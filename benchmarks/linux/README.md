# Linux benchmarks

Book data structures and producer/consumer queues, measured on Linux with thread pinning, a cycle-counter latency
timer and (once enabled) `perf` hardware counters.

- Machine: ThinkPad X1 Carbon Gen 8, Intel i7-10510U (4 cores / 8 threads, Comet Lake), kernel 7.0, g++ 15.2, `-O2 -g`
  (`RelWithDebInfo`). Full state in [`book_env.txt`](book_env.txt) and [`queue_env.txt`](queue_env.txt).
- Governor `powersave`, turbo on, laptop. Book gaps below are large and stable across runs. Absolute queue throughput
  moved up to 2x between sessions, so trust the queue *rankings* more than the queue *numbers*.
- `perf_event_paranoid` was 4 on this run, so hardware counters were skipped. Everything else here was measured.

Contents: [Book results](#book-results) · [Queue results](#queue-results) · [How each queue works](#how-each-queue-works)
· [How the measurements work](#how-the-measurements-work) · [Reproducing](#reproducing) · [Raw data](#raw-data)

---

## Book results

### What is compared

Every variant is a `BasicBook<SecurityBookT, CacheSlots>` driven through `book_for(instrument)`, the same path
`databento_replay` uses. All of them replay the identical sequence of operations. The registry is in
`Core/Source/BookVariants/BookVariants.h`.

| name | price levels | order index | how orders are stored |
|---|---|---|---|
| `tree` | `std::map` | node `std::unordered_map` | `std::list` per level |
| `flat` | sorted vector, linear scan from the touch | node | list |
| `hybrid` | sorted vector, 8 linear probes then binary | node | list |
| `agg` | hybrid vector of `{quantity, count}` | node | aggregate: the order lives only in the index |
| `agg-flat` | hybrid vector | open addressing | aggregate |
| `ladder` | `PriceLadder`, one slot per tick | open addressing | aggregate |
| `ladder-identity` | ladder | open addressing, identity hash | aggregate |
| `pooled-hybrid` | hybrid vector of level slots | open addressing | intrusive FIFO per level, all orders in one vector |
| `pooled-ladder` | ladder of level slots, tick inferred | open addressing | intrusive FIFO |

All of the above use a 64-slot instrument cache. `tree-nocache`, `ladder-nocache` and `ladder-cache1` vary only the
cache.

The two `pooled-*` books (`Core/Source/BookVariants/PooledBook.h`) test one question: what does keeping the real
per-level order queue cost, if it costs no allocations? Each order is a node in one pooled vector, linked into its
level by 32-bit indices, so a delete is an O(1) unlink.

Every timed operation also reads `best_bid()` / `best_ask()`, the way a strategy would after each update. A hash of
those reads plus the final 10-level depth is each run's checksum. **Every variant produced the same checksum in every
scenario**, and `BookVariants.EveryVariantMatchesTreeOnGeneratedFlow` checks the same thing op by op against `tree`.

### Scenarios

The ITCH fixture can't tell these structures apart: 50 fixed levels, and no order is ever removed. So `book_bench`
generates its own flow (`Core/Source/BookVariants/OrderFlow.cpp`):

- each instrument has a fair price that steps one tick with probability `--move` per step. The level it crosses is
  cleared: 2 orders execute, the rest are pulled (quotes fading).
- adds land at a geometric distance from the fair price (`--near-touch`), with `--far-fraction` of them 20 to
  `--far-ticks` ticks away
- 85% of cancels and replaces hit the 64 newest orders (`--recent-cancel`): short-lived quotes, like a real feed
- book size is held near `--resting` orders per instrument

The default mix is about 42% add, 37% delete, 9% execute, 7% partial cancel and 5% replace, with the BBO changing on
about 4% of ops. 2M timed operations per scenario.

| scenario | setup | what it stresses |
|---|---|---|
| `equity` | 5,000 resting, 1 instrument | baseline |
| `thin` | 500 resting | everything in L1/L2 |
| `deep` | 50,000 resting | index and order storage spill out of L2 |
| `volatile` | touch moves 5x as often, denser levels | level create/erase at the touch |
| `wide` | 5% of adds 20..5,000 ticks away | sparse far levels (ladder size, vector shifts) |
| `cme6` | 6 instruments x 5,000, interleaved | like the six-instrument CME hour |
| `multi` | 64 instruments x 2,000, interleaved | cross-book cache pressure, 64-slot cache collisions |
| `random_ids` | random 63-bit order ids | hash behavior without sequential ids (CME-style) |

### Throughput

Median ns per operation over 5 runs (lower is better). Each run is its own process pinned to CPU 1.

| | equity | thin | deep | volatile | wide | cme6 | multi | random_ids |
|---|---|---|---|---|---|---|---|---|
| `tree` | 135 | 110 | 127 | 125 | 138 | 151 | 354 | 138 |
| `flat` | 138 | 110 | 135 | 127 | 147 | 147 | 322 | 127 |
| `hybrid` | 136 | 111 | 129 | 125 | 144 | 150 | 323 | 137 |
| `agg` | 103 | 96 | 93 | 95 | 115 | 110 | 225 | 111 |
| `agg-flat` | 57 | 51 | 48 | 43 | 62 | 58 | 114 | 59 |
| `ladder` | 48 | 42 | 46 | 43 | 50 | 54 | **107** | 51 |
| `ladder-identity` | 62 | 51 | **195** | 62 | 64 | 52 | 109 | 52 |
| `pooled-hybrid` | 48 | 42 | 46 | 41 | 55 | 55 | 115 | 53 |
| `pooled-ladder` | **45** | **39** | **41** | **41** | **46** | **48** | 116 | **49** |

![book heatmap](book_heatmap.png)

Per-scenario bars: [equity](book_equity.png) · [thin](book_thin.png) · [deep](book_deep.png) ·
[volatile](book_volatile.png) · [wide](book_wide.png) · [cme6](book_cme6.png) · [multi](book_multi.png) ·
[random_ids](book_random_ids.png)

![equity](book_equity.png)

### Latency per operation

Cycle counter around every operation, including `book_for` and the BBO read. ns, `equity`:

| | p50 | p90 | p99 | p99.9 |
|---|---|---|---|---|
| `tree` | 162 | 257 | 437 | 739 |
| `hybrid` | 131 | 214 | 347 | 551 |
| `agg` | 105 | 180 | 297 | 468 |
| `agg-flat` | 72 | 119 | 208 | 328 |
| `ladder` | 69 | 105 | 174 | 274 |
| `ladder-identity` | 69 | 129 | 234 | 406 |
| `pooled-hybrid` | 64 | 111 | 207 | 326 |
| `pooled-ladder` | **63** | **90** | **154** | **248** |

`deep` (50,000 resting orders):

| | p50 | p90 | p99 | p99.9 |
|---|---|---|---|---|
| `tree` | 155 | 260 | 487 | 813 |
| `agg-flat` | 68 | 135 | 237 | 418 |
| `ladder` | 68 | 102 | **189** | **357** |
| `ladder-identity` | 66 | 304 | **2,493** | **20,221** |
| `pooled-ladder` | 70 | 148 | 309 | 470 |

![latency, equity](book_latency_equity.png)
![latency, deep](book_latency_deep.png)

### Instrument cache

![routing](book_routing.png)

### What the book numbers say

- **The order index is still the biggest lever.** `agg` → `agg-flat` (node hash map → open addressing) saves ~45 ns in
  every scenario, the largest single step in the table. The same ranking as the CME hour on the Mac.
- **With the node index, the level structure doesn't matter.** `tree`, `flat` and `hybrid` are all 110–150 ns: the
  node hash map and the `std::list` allocations hide the level search. Level storage only shows up once the index is
  fast (`agg-flat` 57 → `ladder` 48).
- **`ladder-identity` breaks on deep books.** At 50k orders its median is 195 ns, its p99 2.5 µs and its p99.9 20 µs,
  13x and 57x worse than `ladder`. Sequential ids with an identity hash fill one contiguous run of the table, and
  every miss or insert walks it. The CME hour showed the start of this; a deep book makes it a tail-latency problem.
  Drop it, or use a real mixing hash.
- **Keeping the full queue costs nothing measurable.** `pooled-ladder` keeps every order in time priority, like
  `tree`, yet matches or beats the aggregate `ladder` in 6 of 8 scenarios and has the lowest p99 in `equity`. The
  aggregate design isn't needed for speed: pooled nodes, index links and a flat index get the same result while
  keeping order-by-order depth.
- **Where pooled loses: `multi` (116 vs 107) and the deep tails (p99 309 vs 189).** The order nodes are a second
  memory region next to the index, so with 64 books or 50k orders there are more cache misses per op. Per-op hardware
  counters would show this directly (see [How the measurements work](#how-the-measurements-work)).
- **Instrument cache:** for `ladder`, going from no cache to 64 slots saves ~12 ns per op in every scenario. With 64
  instruments, a 1-slot cache is about the same as none (the instrument nearly always changes), and 64 slots wins by
  ~8 ns. `tree` gains ~15 ns from the cache, about the same absolute amount.
- **`multi` doubles everything.** 64 books x 2,000 orders don't fit in L2 (1 MB here), so most ops miss to L3. All
  the fast books land at 107–116 ns: memory decides there, not the data structure.

---

## Queue results

Every test runs three ways:

- **unpinned**: the scheduler decides
- **cross_core**: producer on CPU 1, consumer on CPU 2 (two physical cores; cache lines move through L3)
- **smt**: producer on CPU 1, consumer on CPU 5 (two hyperthreads of one core, sharing L1/L2)

### Buffers, flat out (M items/s, median of 5)

| buffer | unpinned | cross_core | smt |
|---|---|---|---|
| ring, uncached counters | 74 | 62 | 105 |
| ring, cached counters (`CircularBuffer`) | 134 | 141 | 145 |
| ring, per-slot sequence (`SequencedBuffer`) | 218 | **331** | 162 |
| Vyukov MPMC | 56 | 56 | 47 |

![buffers](queue_buffers.png)

### Buffers, paced (p50 / p99 ns)

| buffer | 100k/s cross_core | 100k/s smt | 1M/s cross_core | 1M/s smt |
|---|---|---|---|---|
| ring, uncached counters | 216 / 6,064 | 77 / 114 | 167 / 2,353 | 71 / 107 |
| ring, cached counters | 176 / 6,921 | 79 / 110 | 125 / 2,460 | 74 / 113 |
| ring, per-slot sequence | 189 / 8,157 | 81 / 169 | 154 / 6,623 | 73 / 115 |
| Vyukov MPMC | 192 / 7,032 | 93 / 168 | 159 / 640 | 80 / 115 |

![buffer latency](queue_buffers_latency.png)

### Idle strategies (on the cached-counter ring; mutex for reference)

| | flat out cross / smt (M/s) | p50 / p99 at 10k/s, cross | p50 / p99 at 1M/s, cross | consumer CPU |
|---|---|---|---|---|
| mutex + condvar | 4 / 6 | 3,658 / 372,302 | 2,266 / 7,906 | 3% at 10k/s, 65% at 1M/s |
| yield | **210 / 367** | 394 / 3,018 | 345 / 728 | 100% |
| spin | 86 / 298 | **196 / 574** | **140 / 216** | 100% |
| back-off (`BalancedBackoff`) | 88 / 239 | 412 / 1,381 | 137 / 212 | 100% |

![idle strategies](queue_idle.png)

### One producer, N consumers (M items/s)

| consumers | sharded uncached rings | sharded cached rings | sharded sequence rings | MPMC |
|---|---|---|---|---|
| 1 | 51 | 74 | 84 | 67 |
| 2 | 37 | 44 | 50 | 11 |
| 4 | 36 | 48 | 35 | 9 |

### What the queue numbers say

- **Across cores the per-slot sequence ring wins** (331 M/s, 2.3x the cached ring), the same result as on the M2.
- **On hyperthreads the ranking flips**: the sequence ring halves to 162 M/s while the others speed up. See
  [its section](#4-per-slot-sequence-ring-sequencedbuffer) for the likely reason.
- **Hyperthreads halve the handoff latency** (p50 ~75 ns vs ~170 ns) and collapse the p99 from microseconds to ~110 ns,
  because a handoff stays inside one core's L1. The catch is that the two threads share one core's execution units.
- **The cross-core p99 of several µs is mostly the OS** (timer interrupts and scheduler on a laptop), not the queue. It
  barely depends on which buffer is used.
- **Spin vs back-off:** identical at ≥100k/s. At 10k/s back-off has already moved to yielding, so its p99 is 1.4 µs
  against spin's 574 ns. Neither ever reached the sleep stage, so both burned 100% CPU. Only the mutex saves CPU.
- **More consumers only made it slower**, as on the Mac: one producer is the limit, and MPMC collapses at 2 consumers.

---

## How each queue works

All of these pass items from a producer thread to a consumer thread. What separates them is how often a cache line
has to move between the two cores: roughly 50–100 ns per move across cores here, much less between hyperthreads.

### 1. Mutex + condition variable (`MutexQueue`, `Baseline/MutexDecoder`)
A `std::queue` behind a `std::mutex`. Push takes the lock, pushes, then calls `notify_one`. Pop sleeps on the
condition variable until something arrives.
- **Good:** it's the only queue that truly sleeps when idle (3% CPU at 10k/s versus 100% for every ring). It's
  simple, and any number of producers or consumers work.
- **Bad:** every op is a lock and an unlock, and waking a sleeping consumer is a system call plus a scheduler wakeup.
  That gives 4–6 M/s, a 2–4 µs median, and a 370–560 µs p99 at 10k/s, when the consumer is deep asleep. The deque also
  allocates as it grows.

### 2. Ring with uncached counters (`UncachedIndexBuffer` in `Core/Test/QueueBench.cpp`)
A fixed array plus two atomic counters, `head_` (consumer) and `tail_` (producer), each on its own cache line.
- **Good:** no locks, no allocation, and each side writes only its own counter.
- **Bad:** each side reads the other's counter on every op to check full or empty. So every push pulls the consumer's
  line over and every pop pulls the producer's line back: one line move per item. 62 M/s across cores. It's faster on
  hyperthreads (105 M/s) because there the "move" stays inside the shared L1.

### 3. Ring with cached counters (`CircularBuffer`, used by `MessagePipeline`)
The same ring, but each side keeps a private copy of the other's counter (`head_cache_`, `tail_cache_`) and only
re-reads the real one when its copy says the ring is full or empty.
- **Good:** if the producer is 1,000 slots ahead, the consumer reads `tail_` once and then pops 1,000 items without
  touching the producer's line: 141 M/s across cores, 2.3x the uncached ring. `pop_batch` builds on this: read
  `tail_` once, consume a run, store `head_` once.
- **Bad:** when the consumer keeps up and the ring is nearly empty, which is the normal case at real feed rates, the
  copy always says "empty" and it re-reads `tail_` every pop. Under light load it degrades to the uncached ring. The
  data slots themselves still cross cores, which is unavoidable.

### 4. Per-slot sequence ring (`SequencedBuffer`)
There are no shared counters at all. Each slot holds `{sequence, value}`. The producer checks that
`slot.sequence == tail`, writes the value, then publishes with `sequence = tail + 1`. The consumer waits for
`sequence == head + 1`, then frees the slot with `head + Capacity`.
- **Good:** the "ready" flag sits in the same cache line as the data, so one line transfer carries both, and neither
  side ever reads the other's position. The fastest across cores, here and on the M2.
- **Bad:** the slowest on hyperthreads (half its cross-core speed). The likely cause, not yet measured: the two threads
  share one core's pipeline, and the consumer reads exactly the line its sibling is storing to. Intel cores throw away
  in-flight work and restart when a load races a store from the other thread on the same line (a "memory ordering
  machine clear"). `perf stat -e machine_clears.memory_ordering` will confirm or refute it. Each slot also carries
  8 bytes of sequence, so fewer items fit per line.

### 5. Vyukov bounded MPMC (`MpmcQueue`)
Per-slot sequences like #4, plus shared `enqueue_pos` / `dequeue_pos` counters that producers and consumers claim with
compare-and-swap.
- **Good:** the only queue here where *any* consumer can take *any* item, so it's correct with several of each. Decent
  tails at 1M/s (p99 640 ns across cores).
- **Bad:** even with one consumer, every op is a locked atomic read-modify-write, which acts as a full fence: 56 M/s.
  With two consumers they fight over `dequeue_pos`, the CAS keeps failing and the line ping-pongs (67 → 11 M/s). It's
  also wrong for a book: two consumers could apply messages for one instrument out of order. That's why
  `ShardedDecoder` routes by instrument through separate SPSC rings instead.

### Idle strategies (`Core/Source/Core/IdleStrategy.h`)
What a side does when the ring is empty or full. This matters as much as the queue:

| strategy | how it waits | effect |
|---|---|---|
| yield | `sched_yield()` system call | Best flat out: each syscall takes the waiter off the shared line for a while. The worst p50 under load (~350 ns) |
| spin | the `pause` instruction (~140 cycles on Skylake-derived cores) | Lowest latency, 100% CPU. Worst flat out, because the spinner keeps stealing the line from the side doing real work |
| back-off | spin N, then yield M, then sleep 20 µs | Equals spin at high rates. Slower once it reaches the yield stage. Only saves CPU once it reaches the sleep stage |

x86 note: `PauseKind::Isb` compiles to the same `pause` as `PauseKind::Yield`, so the `isb` rows of
`make backoff_sweep` duplicate the `yield` rows here. And `pause` costs ~140 cycles here versus ~9 ns for `isb` on the
M2, so `BalancedBackoff`'s 256 spins last several times longer on this CPU.

---

## How the measurements work

### Pinning each thread to a CPU (`Core/Test/QueueBench.cpp`)
`taskset` pins a whole process, so each thread pins itself instead:

```cpp
void pin_current_thread(std::size_t slot)
{
    static const std::vector<int> cpus = pinned_cpus();   // parsed from LOB_CPUS="1,5"
    if (cpus.empty()) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpus[slot % cpus.size()], &set);
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}
```

The producer uses slot 0 and the consumer slot 1, so `LOB_CPUS=1,5 queue_bench one sequenced 20000000` puts the
producer on CPU 1 and the consumer on CPU 5. In fanout mode, consumers take slots 1..N. Without `LOB_CPUS` nothing is
pinned, which also keeps it a no-op on macOS.

### Finding hyperthread pairs (`tools/linux/common.sh`)
The kernel publishes the CPU layout in `/sys/devices/system/cpu/cpuN/topology/thread_siblings_list` (`1,5` on this
CPU). `setup_cpus` picks:

- `PRIMARY_CPU=1` (avoiding cpu0, which handles most interrupts)
- `CROSS_CPU` = the first CPU not on cpu1's core
- `SMT_CPU` = cpu1's other hyperthread

All three can be overridden by environment variables. Fanout fills physical cores before hyperthreads
(`physical_first_cpus`).

### Is perf allowed?
`/proc/sys/kernel/perf_event_paranoid`: ≤1 means everything, 2 means only your own user-space code, 3–4 (Ubuntu's
default) means nothing. At level 2 the scripts add `:u` to every event (`cycles:u`) so perf counts only user-space
code, which is all that level allows. At 3–4 they skip perf and print the command that enables it.

### Which events
`task-clock, context-switches, cycles, instructions, branch-misses, L1-dcache-load-misses, LLC-loads,
LLC-load-misses`, plus `mem_load_l3_hit_retired.xsnp_hitm` and `dTLB-load-misses` when `perf list` shows the CPU has
them.

The important one for queues is **HITM** ("hit modified"): loads that found their line modified in another core's
cache. That is exactly the line bouncing that separates rings #2, #3 and #4, so per item the uncached ring should be
highest and the sequence ring lowest.

### Counting only the timed loop (`book_bench`)
Plain `perf stat` would also count loading the ~80 MB flow file, the warm-up and the teardown. perf has a control
channel for this: start with counters disabled (`-D -1`) and let the program switch them on and off through a pipe.

Shell side (`perf_stat_gated` in `tools/linux/common.sh`):

```bash
mkfifo "$dir/ctl" "$dir/ack"
exec {ctl}<> "$dir/ctl"; exec {ack}<> "$dir/ack"
LOB_PERF_CTL_FD=$ctl LOB_PERF_ACK_FD=$ack \
  perf stat -x, -e "$(perf_events)" -o "$out" -D -1 --control "fd:$ctl,$ack" -- taskset -c 1 book_bench ...
```

Program side (`PerfCounters` in `Core/Test/BookBench.cpp`):

```cpp
perf_counters().enable();          // write(ctl_fd, "enable\n"), then wait for perf's "ack"
for (...) { apply(book, op); hash = observe(...); }
perf_counters().disable();         // write(ctl_fd, "disable\n")
```

Without the environment variables these calls do nothing. `queue_bench` uses plain `perf stat` around
`queue_bench one <impl> <items>`, since 20M items swamp the setup cost.

### From perf output to per-op numbers
`-x,` makes perf print CSV (`value,unit,event,...`). `perf_to_rows` prefixes each line with the scenario and
implementation, strips `:u`, and blanks `<not counted>` values; the result is `book_perf.csv` / `queue_perf.csv`.
`tools/plot_linux_bench.py` divides by the op or item count to get cycles/op, IPC, misses/op, HITM/item and context
switches per million, and draws `book_perf.png` / `queue_perf.png`.

### `perf c2c` (`queue_bench.sh --c2c`)
`perf c2c record` samples memory loads with their addresses. `perf c2c report` then lists the specific cache lines
with the most cross-core traffic and the code that touches them. This is how to find false sharing like the
`MboSession::records_` problem in `benchmarks/README.md` without reasoning about byte offsets by hand. It usually
needs root.

### Per-op latency (`CycleClock` in `BookBench.cpp`)
It reads the CPU's timestamp counter around each operation, with `lfence` so the CPU can't reorder the reads around
the measured op:

```cpp
_mm_lfence(); t = __rdtsc(); _mm_lfence();
```

Ticks become ns via a calibration against `steady_clock` over a 100 ms sleep. The timer's own cost is recorded as
`clock_overhead_ns` in `book_latency.csv`. On ARM it falls back to `steady_clock`.

### Keeping the numbers honest
- Each book run is a separate process, so one implementation's heap state can't affect the next. The run order
  rotates every round.
- Every book run's checksum must match, or the script fails.
- The flow is generated once per scenario and saved (`benchmarks/linux/flows/`, gitignored, ~620 MB), so every
  implementation sees the identical byte stream.
- `*_env.txt` records the governor, turbo, power, compiler and git commit. The scripts warn when these will add noise.

---

## Reproducing

### Linux
```
sudo apt install cmake ninja-build linux-tools-$(uname -r)
sudo sysctl kernel.perf_event_paranoid=1                      # hardware counters
sudo cpupower frequency-set -g performance                    # optional, steadier clocks
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo   # optional

make book_report                    # ~5 min
make linux_queue_report             # ~10 min
make book_report BOOK_ARGS="--runs 3 --scenarios equity,deep --itch testdata/session_mix.bin"
make linux_queue_report QUEUE_ARGS="--c2c --fanout"
tools/linux/book_bench.sh --help
```

### macOS
`book_bench` builds and runs there too. Pinning is a no-op, and latency falls back to `steady_clock` on ARM:

```
make RELEASE=1 book_bench
./build-cmake/release/book_bench list
./build-cmake/release/book_bench describe --resting 50000
./build-cmake/release/book_bench throughput --resting 50000 --runs 5 --impl ladder,ladder-identity,pooled-ladder
./build-cmake/release/book_bench latency --resting 50000 --runs 1 --impl ladder,ladder-identity
python3 tools/plot_linux_bench.py book benchmarks/linux     # redraw plots from the CSVs here
```

The shell scripts depend on `/sys` and `perf`, so they are Linux-only.

---

## Raw data

| file | contents |
|---|---|
| [`book_throughput.csv`](book_throughput.csv) | scenario, impl, levels, index, orders, cache, run, ops, ms, mops, ns_per_op, teardown_ms, checksum |
| [`book_latency.csv`](book_latency.csv) | per scenario x impl x op type: count, p50/p90/p99/p99.9/max/mean ns |
| [`book_perf.csv`](book_perf.csv) | perf counters per scenario x impl (empty on this run) |
| [`book_summary.md`](book_summary.md) | median tables |
| [`queue_buffers.csv`](queue_buffers.csv) | placement x buffer: flat-out M/s and paced p50/p99/p99.9/CPU |
| [`queue_idle.csv`](queue_idle.csv), [`queue_idle_latency.csv`](queue_idle_latency.csv) | idle strategies by placement |
| [`queue_fanout.csv`](queue_fanout.csv) | 1 producer → N consumers |
| [`queue_perf.csv`](queue_perf.csv) | perf counters per placement x buffer (empty on this run) |
| [`queue_summary.md`](queue_summary.md) | median tables |
