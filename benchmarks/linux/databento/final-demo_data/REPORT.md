# Final demo report: the Databento path

Everything behind the seven charts in [`../final-demo_images/report/`](../final-demo_images/report/): what each one
shows, how it was measured, the numbers, what to say on the slide, and the caveats. Written so it can be read on its
own, without the session that produced it.

- **Charts**: `01_handoff.png` … `07_whole_flow_counters.png`, 150 dpi PNGs on a near-white background with no
  figure title, so each drops onto a slide under the slide's own title. Colors are fixed across charts: direct call
  green, mutex queue red, ring buffer orange (spin) / gold (yield) / blue (back-off) / violet (tuned back-off); books
  gray (`std::map`), orange (`std::vector`), blue (ladder). Where a chart has two threads, the producer is the light
  bar and the consumer the dark one.
- **Numbers**: [`RESULTS.md`](RESULTS.md) has every number behind every chart as tables, generated from the CSVs by
  `python3 tools/report_tables.py`. Quote from there; the numbers below are rounded from it.
- **Redraw**: `python3 tools/plot_report.py`. **Recollect**: `tools/linux/databento_report_data.sh` (about 70 min).

Contents: [Conclusions](#1-conclusions-about-the-whole-book) · [The charts](#2-the-charts) ·
[Why these counters](#3-why-these-counters-04-and-07) · [Setup and method](#4-setup-and-method) ·
[Files and columns](#5-files-and-columns) · [Regenerating](#6-regenerating) · [Caveats](#7-caveats) ·
[Older files here](#8-older-files-in-this-folder)

---

## 1. Conclusions about the whole book

Numbers are from this run (laptop, ladder book unless said otherwise); detail per chart below.

1. **Hand off with a lock-free ring, or not at all.** Decode on one core and book on another, joined by a lock-free
   single-producer ring: 14.3–14.6 M msgs/s, **1.7x** the single-threaded direct call (8.4). A mutex + condition
   variable queue is **2.5x slower than one thread** (3.4 M msgs/s): each message costs ~1,000 cycles on *each*
   thread, over half of them in the kernel putting the consumer to sleep and waking it (~70 context switches per
   1,000 messages), plus ~1 cache line pulled between cores per message (07). The ring's handoff costs 24–30 cycles
   a message.
2. **With a ring, the pipeline is balanced, so the book only helps up to a point.** Producer ~205–225 cycles per
   message (decode), consumer ~235 (book). The book is the limit by a hair: `std::map` → `std::vector` buys 26%
   (11.1 → 14.1 M/s), `std::vector` → ladder only 1–4%, although the ladder is 17% faster on its own. Beyond ~14 M/s
   decode must get faster too.
3. **The ladder is the best inner book**: 18.6 M msgs/s alone, 74 / 135 / 229 ns at p50 / p90 / p99, vs 11.9 M/s
   for `std::map`. All three do the same ~70–80 cycles of useful work per update; the ladder wastes the least
   (48 cycles of branch mispredictions vs 83 for the map) and wins most on modify (77 ns vs 118 / 140).
4. **The next bottlenecks are the order index and decode.** In every book the order-ID hash table causes most slow
   memory loads (47–64%), and the book core runs at ~0.9 instructions per cycle against ~3 on the decode core.
   Decode (Databento SDK + parse) costs as many cycles per message as the book.
5. **Latency: the queue mechanism matters more than the book.** Median from arrival until in the book, paced at 60x:
   ring 1.6–2.7 µs, direct call 6.6–9.4 µs, mutex 55–75 µs. The direct call has no handoff (its book update alone is
   ~310 ns), but one thread at 8 M/s falls behind in bursts and messages wait to be decoded; the mutex pays a kernel
   wake-up (~37 µs median handoff). Book choice moves the median by at most ~30%.
6. **The idle strategy depends on who else needs the core.** Cores to itself: spin is fastest (402 ns median
   handoff) but burns a core; back-off frees about a third of the consumer core for ~2x the median. 4 order books on the same
   2 cores: spin collapses (31 µs median, 0.7 ms p90, and the books together process less: 13.1 vs 14.4 M/s), the
   tuned back-off, which spins 0.5 ms first, nearly as badly (5.6 µs, 0.44 ms p90, 12.6 M/s); yield and back-off keep
   the throughput (14.6–15.0 M/s) and the median at 1.8–5.3 µs, because giving up the core hands it to a book that
   has work.
7. **What to run on this machine:** ring + ladder; spin when the two cores are dedicated, back-off (or yield) when
   order books share cores; shard by instrument when there are spare cores (earlier run: 2 shards, 438 ns median).

---

## 2. The charts

Common to all: CME `cme_mix` MBO data (1,054,337 records), producer thread on CPU 1 and consumer on CPU 2 (two
separate physical cores), books behind a 64-slot instrument cache, consumer batch 64. "Flat out" = the file replayed
as fast as possible (throughput, counters); "paced" = replayed at 60x market time (latency, CPU busy). Details in
[Setup and method](#4-setup-and-method).

### 01 · Handoff (`01_handoff.png`)

**Suggested slide title:** *A second thread only pays with a lock-free ring.*

**Shows.** Left: throughput flat out, ladder book, M messages/s (median of 5 runs, whisker min–max). Right: median
handoff latency, push → pickup, paced at 60x, log scale (median of 2 runs). Direct call: 0, there is no handoff.

| ladder book | direct call | mutex queue | ring, spin | ring, yield | ring, back-off |
|---|---|---|---|---|---|
| M msgs/s | 8.4 | 3.4 | 14.3 | **14.6** | 14.4 |
| median push → pickup | 0 | 36.5 µs | **392 ns** | 538 ns | 754 ns |
| median decoded → in book | 308 ns | 36.9 µs | 494 ns | 658 ns | 862 ns |
| consumer core busy (paced) | – | 10% | 100% | 100% | 65% |

**What to say.**
- A second thread helps only if the handoff costs less than the work it moves. The ring costs ~25–30 cycles per
  message (07) and moves the ~185-cycle book update to another core: 1.7x the direct call.
- The mutex queue is slower than one thread: 3.4 M msgs/s. Its median wait, 36.5 µs, is the kernel waking the
  consumer, which sleeps whenever the queue is empty (most of the time in a paced feed).
- Flat out, the three idle strategies are equal (the consumer never runs dry). Paced, they differ at the median:
  spin 392 ns, yield 538 ns (nothing else to run, so `sched_yield` returns at once: a slower spin), back-off 754 ns
  (it naps in long gaps, freeing a third of the core).

**Careful.** Push → pickup is the handoff only; the end-to-end comparison including the direct call's backlog is 06.

### 02 · Idle strategy: cores to itself vs shared with other order books (`02_idle_strategy.png`)

**Suggested slide title:** *Spin when the cores are yours; back off when other order books share them.*

**Shows.** The ring's idle strategies (what the consumer does when there is nothing to read) with 1, 2 and 4 order
books on the same 2 cores (dark → light). An order book = one producer and one consumer thread. Panels: throughput of
all order books together, flat out; median and p90 handoff latency, push → pickup, paced at 60x (log); the two
cores' busy %, paced. The tuned back-off is `TimedBackoff` (spin 0.5 ms, then 5 µs naps).

**How.** N copies of `databento_replay` started together on the same file. Book k: producer on CPU 1 and consumer on
CPU 2 if k is even, swapped if odd (2 books: each core runs one book's producer and the other's consumer; 4 books:
two of each). Throughput: 5 flat-out runs, combined = all records / the slowest book's seconds. Latency: 2 paced runs,
median over books and runs. Busy: ref-cycles / TSC on CPUs 1 and 2, averaged.

| median (p90) push → pickup | 1 book | 2 books | 4 books | M msgs/s combined, 1 → 4 books |
|---|---|---|---|---|
| spin | **402 ns** (3.8 µs) | **494 ns** (4.2 µs) | 31 µs (**717 µs**) | 14.4 → **13.1** |
| yield | 516 ns (4.1 µs) | 4.1 µs (23 µs) | 5.3 µs (28 µs) | 14.7 → 14.6 |
| back-off | 772 ns (55 µs) | 4.3 µs (48 µs) | **1.8 µs** (17 µs) | 14.5 → **15.0** |
| tuned back-off | 735 ns (8.0 µs) | 630 ns (7.8 µs) | 5.6 µs (441 µs) | 14.2 → 12.6 |

Busy (both cores averaged): 1 book: spin / yield 52% (consumer core 100%), back-off 35%, tuned 42%. 2 and 4 books:
spin and yield 100%, back-off 72% / 99%, tuned 81% / 98%.

**What to say.**
- Cores to itself: spin is fastest, at the price of a core at 100%. Yield is a slower spin (nothing to yield to).
  Back-off gives the core back between bursts at ~2x the median.
- Two books: each core holds one consumer and another book's mostly-sleeping producer. Spin still wins (494 ns);
  yield and back-off now cost ~4 µs, because once they give the core up they wait to be scheduled again.
- Four books: two consumers per core. A spinning consumer burns its whole time slice while the one next to it has
  messages waiting: 31 µs median, 0.7 ms p90, and the books together get less done (13.1 M/s). The tuned back-off
  spins 0.5 ms first, so it behaves like spin (0.44 ms p90, 12.6 M/s). Back-off and yield hand the core to a
  consumer that has work: 1.8 / 5.3 µs median, and combined throughput holds at 14.6–15.0 M/s.
- So the best strategy flips with contention: spin for dedicated cores, back-off once order books outnumber cores.

**Careful.** p99 is left out: it is 6–21 ms for every strategy and measures the start-up snapshot and burst
queueing, not the strategy (see Caveats). Yield and back-off with 4 books cost the kernel ~1 M context switches per
second per core. With an *unrelated* busy process on the core instead of other order books, the earlier run found
yield and back-off wait a whole time slice (~1.5 ms) and the tuned back-off wins (section 8).

### 03 · Inner book (`03_inner_book.png`)

**Suggested slide title:** *The ladder book is fastest: 18.6 M msgs/s, 74 ns median.*

**Shows.** Left: throughput of each book alone (no decode, no handoff), M messages/s. Right: latency of each book
update, p50 / p90 / p99 in ns (solid / medium / light bars).

**How.** Left: `databento_replay <file> … variants all` decodes the file once into memory, then replays the records
into each book 15 times; the bar is 1000 / median ns per record. Right: the same records exported as a `book_bench`
flow (`flows/cme_mix.flow`, 907,454 book operations) and replayed with a cycle counter read around every operation
(`book_bench latency`, 3 runs, medians of the run percentiles). Both single-threaded, pinned to CPU 1.

| | throughput | ns / message | p50 | p90 | p99 |
|---|---|---|---|---|---|
| `std::map` | 11.9 M/s | 84 | 106 ns | 178 ns | 308 ns |
| `std::vector` | 15.9 M/s | 63 | 76 ns | 154 ns | 261 ns |
| ladder | **18.6 M/s** | **54** | **74 ns** | **135 ns** | **229 ns** |

**What to say.**
- The three books are identical except for how one side's price levels are stored: a red-black tree (`std::map`),
  a sorted array searched from the best price (`std::vector`), or an array indexed by price (ladder). All three end
  with the same book (the `depth_hash` column of `inner_book_variants.csv` matches for every variant).
- The ladder is 1.56x the map and 1.17x the vector. Its median equals the vector's (most updates land near the top of
  the book, where the vector's 8-step search from the best price is as cheap as an index), but its tail is shorter:
  the vector shifts every better level when a level is created or emptied; the ladder doesn't.
- Every other variant measured (node-based orders, linear search, pooled FIFO, lazy queues, no instrument cache …)
  is in `inner_book_variants.csv`. Turning off the 64-slot instrument cache costs the ladder 23%
  (`ladder-nocache` 66 ns vs 54 ns).

**Careful.** Per-op latencies include about 30 ns of timer overhead (`clock_overhead_ns`, reported, not subtracted).
They are "book only": the same books inside the full pipeline are in 06.

### 04 · Inner book counters (`04_inner_book_counters.png`)

**Suggested slide title:** *Why the ladder wins: the same work, fewer wasted cycles.*

**Shows.**
1. *Where each book update's cycles go*: total CPU cycles per book update, split into what the core was doing
   (Intel's top-down method, below).
2. *Slow memory loads, by data structure*: of all loads that took 30 cycles or more, the share of their total
   latency that belongs to each data structure.
3. *Median cost per operation*: add, cancel and modify separately, in ns.

**How.** `book_bench throughput` on the exported flow, pinned to CPU 1, perf counting only the timed loop (gated, see
[Counters](#counters)). Panel 1: `perf stat -M tma_<metric>`, one metric per run, 6 passes each (Intel's top-down
level 1, plus the memory split). Panel 2: `perf record -e cpu/mem-loads,ldlat=30/P -c 199`: a sample of every 199th
load that took ≥ 30 cycles, with its source line, mapped to the data structure that line touches
(`tools/linux/perf_mem_sources.py`). Panel 3: the per-op latency run from 03.

Reading panel 1. Each cycle a core can retire up to 4 micro-ops. Top-down sorts every issue slot into:
**useful work** (retiring: real instructions completing), **mispredicted branches** (work thrown away after a wrong
guess), **instruction supply** (front end: the core waited for instructions to be fetched and decoded),
**waiting on caches** / **waiting on DRAM** (back end, memory: a load hadn't arrived), and **execution units** (back
end, core: dependency chains and busy ports). The chart scales the percentages to the measured cycles per update, so
the stacks add up to the cycle count and are in the same unit.

| per book update | `std::map` | `std::vector` | ladder |
|---|---|---|---|
| cycles | 314 | 272 | **232** |
| useful work | 74 | 82 | 70 |
| mispredicted branches | 83 | 72 | **48** |
| instruction supply | 70 | 64 | **49** |
| waiting on caches + DRAM | 27 + 11 | 14 + 8 | 17 + 12 |
| execution units | 48 | 32 | 36 |
| branch misses (count) | 4.3 | 3.6 | **2.2** |
| instructions | 316 | 298 | **215** |

| slow loads (share of latency) | `std::map` | `std::vector` | ladder |
|---|---|---|---|
| order index (order ID → resting order) | **64%** | **50%** | **47%** |
| price levels | 14% | 3% | 1% |
| shifting & growing arrays (memmove, vector growth) | 0% | 24% | 28% |
| book logic | 12% | 8% | 5% |
| reading the input (benchmark harness) | 2% | 9% | 7% |
| other | 8% | 6% | 12% |

| median per op | add | cancel | modify |
|---|---|---|---|
| `std::map` | 109 ns | 90 ns | 140 ns |
| `std::vector` | 76 ns | 65 ns | 118 ns |
| ladder | 77 ns | 68 ns | **77 ns** |

**What to say.**
- All three books do about the same useful work, 70–82 cycles per update. The differences are all waste: the map
  wastes 240 cycles per update, the ladder 160.
- The biggest waste is branch mispredictions: finding a level in a tree is a chain of compares the CPU can't predict
  (83 cycles for the map); the ladder computes the slot from the price (48 cycles, 2.2 misses per update vs 4.3).
- The order index is the biggest source of slow memory loads in every book, 47–64%: every cancel and modify looks
  the order ID up in a hash table, a random access into tables holding up to ~54,000 resting orders. That's the next
  thing to optimise, whichever book you pick.
- Modify (17% of operations) is where the ladder pulls away: 77 ns vs 118 / 140. A price change removes from one
  level and adds to another, which the ladder does by index; the others search twice and may shift or rebalance.

**Careful.** Top-down percentages are of issue slots and the memory split is approximate (DRAM share of the memory
bucket = `tma_dram_bound` / sum of the L1/L2/L3/DRAM/store-bound metrics). The slow-load panel is a share of
latency, not of time: the core overlaps some of it with other work. "Reading the input" is book_bench reading the
next operation from the flow, not book code.

### 05 · Handoff counters (`05_handoff_counters.png`)

**Suggested slide title:** *What a handoff costs the CPU, per item.*

**Shows.** Six counters per item handed over, for the producer (light) and consumer (dark) thread, plus the direct
call (one green bar: one thread does both): cycles, instructions, locked atomic instructions, loads that hit a cache
line modified by the other core (HITM), pipeline flushes for memory ordering, and context switches for the whole run.

**How.** `queue_bench one <impl> 20000000`: 20 M integers through each handoff, flat out, producer on CPU 1 and
consumer on CPU 2, `perf stat -C 1,2 -A` (per CPU = per thread), 3 runs, median, divided by 20 M. Integers make the
item free to produce and consume, so everything counted is the handoff itself. The direct call is a loop that calls
the consume function on each item (compiler-visible, so it's inlined): the floor.

| per item | direct | mutex queue | ring, spin | ring, yield | ring, back-off |
|---|---|---|---|---|---|
| cycles (producer / consumer) | 1.1 | 634 / 634 | 30 / 31 | 11 / 11 | 24 / 24 |
| instructions | 5 | 430 / 451 | 17 / 10 | 17 / 11 | 20 / 14 |
| locked atomics | 0 | 3.5 / 3.1 | 0 / 0 | 0 / 0.002 | 0 / 0 |
| loads hitting the other core's modified line | 0 | 1.39 / 1.19 | 0 / 0.25 | 0 / 0.04 | 0 / 0.16 |
| memory-ordering flushes | 0 | 0.51 / 0.47 | 0.001 / 0.09 | 0.002 / 0.013 | 0.005 / 0.06 |
| context switches (run) | 5 | 4,889 / 168,330 | 13 / 15 | 21 / 15 | 13 / 19 |

**What to say.**
- A ring buffer hands an item over in 11–31 cycles with plain loads and stores: on x86 the release store that
  publishes a slot and the acquire load that reads it need no locked instruction.
- The mutex queue costs ~634 cycles per item on each thread: 3+ locked instructions per item (lock, unlock, the
  condition variable), the lock and queue cache lines bouncing between the two cores (1.2–1.4 HITM loads per item),
  and, whenever the consumer catches up and the queue is empty, the kernel puts it to sleep and wakes it again
  (168,330 context switches for 20 M items).
- Memory-ordering flushes: when a core has speculatively read a line that the other core then writes, it throws the
  speculative work away. The spinning consumer gets one every ~11 items; the mutex gets one every ~2.
- In this microbenchmark yield is the cheapest ring (11 cycles): while the consumer is inside `sched_yield()` the
  producer fills a run of slots, which the consumer then drains in one go, so lines move between cores less often.
  Spin polls the producer's line constantly (0.25 HITM per item). On real messages (07) all three cost the same.

**Careful.** Not gated: the counts include thread start and stop, which is negligible against 20 M items. The direct
call's 1 cycle/item is a function call the compiler inlined, the cost of "no handoff".

### 06 · Every handoff × every inner book (`06_handoff_x_inner_book.png`)

**Suggested slide title:** *The handoff sets the scale, the book sets the ceiling: ring + ladder.*

**Shows.** All 15 combinations, everything else fixed (file, cores, batch 64, instrument cache). Left: throughput
flat out (median of 5, whisker min–max). Right: median latency from the message's arrival until it is in the book,
paced at 60x, log scale (median of 2 runs).

| M msgs/s, median arrival → in book | `std::map` | `std::vector` | ladder |
|---|---|---|---|
| direct call | 7.1, 9.4 µs | 8.1, 6.8 µs | 8.4, 6.6 µs |
| mutex queue | 3.2, 74.7 µs | 3.1, 73.4 µs | 3.4, 55.4 µs |
| ring, spin | 11.1, 1.8 µs | 14.1, 1.6 µs | 14.3, **1.6 µs** |
| ring, yield | 11.3, 2.0 µs | 14.0, 1.9 µs | **14.6**, 1.9 µs |
| ring, back-off | 11.2, 2.7 µs | 14.0, 2.4 µs | 14.4, 2.3 µs |

**What to say.**
- The handoff sets the scale: the mutex queue is ~3 M msgs/s and ~55–75 µs whatever the book.
- With a ring, the book matters while it is the slower stage: `std::map` caps the pipeline at ~11 M/s; `std::vector`
  and ladder both reach ~14 M/s, where decode is about as slow as the book (07).
- The direct call has no queue, but its single thread falls behind in bursts and messages wait to be decoded: 6.6 µs
  median from arrival, although the book update itself takes ~310 ns. The ring keeps up, so its messages reach the
  book 4x sooner (1.6 µs).
- Best: ring + ladder, 14.3–14.6 M/s and 1.6–2.3 µs.

**Careful.** Arrival is the record's scheduled time in the paced replay, but not before the producer last woke
(section 4). Tails are not compared: see Caveats (start-up snapshot). `RESULTS.md` also has decoded → in book.

### 07 · The whole flow, per message (`07_whole_flow_counters.png`)

**Suggested slide title:** *Where a message's CPU time goes, end to end.*

**Shows.** The same layout as 05, but on the real flow (decode → handoff → ladder book) and per message:
1. CPU cycles per message, per thread.
2. Where each message's cycles go, both threads stacked: decode (Databento SDK + parse), handoff & waiting, book
   update, OS (kernel), other.
3. Instructions per cycle (IPC), per thread.
4. Loads served from DRAM per message.
5. Cache lines pulled from the other core per message (loads that hit a line modified by the other core, HITM).
6. Context switches per 1,000 messages.

**How.** `databento_replay` flat out, ladder book, each handoff, `perf stat -C 1,2 -A`, counting only the replay loop
(gated), 3 event groups x 5 runs, median, divided by records. Panel 2: `perf record -F 4999 -C 1,2` over one run per
handoff; each sampled function is put in a stage by its name (`stage_of` in `plot_report.py`); the stage shares
are then scaled to the counted cycles. For the direct call only CPU 1 is used (CPU 2 is idle).

| per message | direct | mutex queue | ring, spin | ring, yield | ring, back-off |
|---|---|---|---|---|---|
| cycles (producer / consumer) | 409 | 997 / 988 | 225 / 236 | 223 / 234 | 207 / 234 |
| both threads | 409 | 1,985 | 461 | 458 | 441 |
| decode | 169 | 213 | 191 | 171 | 141 |
| handoff & waiting | 0 | 351 | 24 | 25 | 30 |
| book update | 179 | 283 | 189 | 190 | 183 |
| OS | 57 | **1,055** | 53 | 68 | 81 |
| IPC (producer / consumer) | 1.86 | 1.59 / 1.05 | 2.83 / 0.92 | 3.19 / 0.93 | 3.18 / 0.92 |
| DRAM loads (producer / consumer) | 0.12 | 0.02 / 0.14 | 0.02 / 0.14 | 0.02 / 0.14 | 0.02 / 0.14 |
| lines pulled from the other core | 0 | 0.91 / 1.15 | 0.007 / 0 | 0.006 / 0 | 0.005 / 0 |
| context switches / 1,000 msgs | 0 | 73.5 / 70.2 | 0 / 0 | 0 / 0 | 0.2 / 0 |

**What to say.**
- With a ring, splitting the work over two cores costs only ~35–50 more cycles per message in total than doing it
  all on one (441–461 vs 409), and it gives each core one job: ~205–225 cycles per message on the producer (decode
  and push), ~235 on the consumer (book update). Throughput follows the slower of the two: 409 / 236 = 1.7x.
- The two jobs are very different: decode runs at ~3 instructions per cycle, the book at ~0.9. The book is limited by
  memory (almost all DRAM loads are the consumer's: 0.14 per message, mostly the order index) and by branches
  (~2 mispredictions per message). Decode is limited by the amount of work.
- The mutex queue turns the OS into the biggest stage: 1,055 of 1,985 cycles per message are kernel time (futex
  sleep and wake-up, scheduling), plus 351 in locking and the queue's memory allocation; ~70 context switches per
  1,000 messages on each thread; ~1 cache line stolen from the other core per message. Even the book update takes
  longer (283 vs ~185 cycles), most likely because the kernel code that runs between batches pushes the book out of
  the core's caches: the mutex consumer has 5x the L2 misses per message of a ring consumer (2.8 vs 0.55).
- The ring moves almost no cache lines between cores: when it is deep, the consumer reads slots that the producer
  wrote long enough ago that they have left the producer's private caches, so they come from the shared L3, not
  from the other core.

**Careful.** The stage split is a sampling profile, assigned by function name. The producer's push loop is inlined
into its per-record callback, so for spin and yield the producer's waiting for a free slot is counted as decode
(the ring is full when the consumer is the slower stage); back-off sleeps instead, which shows as OS. Kernel symbols
were hidden (`kptr_restrict`), so kernel time is one slice: reading the file, page faults, futex and sleep calls,
and the idle loop when a thread is parked.

---

## 3. Why these counters (04 and 07)

**04, the inner book.** Raw counters (misses per op, IPC) say that the map has more branch misses but not how much
time that costs or what to change. The three panels answer, in order: *how much time is wasted and on what*
(top-down, scaled to cycles so the stacks add up to the measured cost and compare across books), *which data
structure the memory stalls come from* (perf mem, attributed by source line: it names the order index as the next
target, in all three books), and *which operation the ladder wins on* (modify). Left out: L1/L2/L3 miss counts
(they don't say which structure missed; they're in `RESULTS.md`), `stalls_mem_any` (overlaps the top-down buckets).

**07, the whole flow.** The six panels mirror 05 (same layout, same colors) on the real flow, and each isolates one
way a handoff can cost time: *CPU work* (cycles per message, per thread), *what that work is* (stage split),
*how efficiently each core runs* (IPC: decode ~3 vs book ~0.9 shows the two halves are different kinds of work),
*memory* (DRAM loads: they belong to the book, whatever the handoff), *coherence* (lines pulled from the other core:
the cost of two threads sharing data), and *the kernel* (context switches). Left out: branch misses (~2 per
message, all in the book, for every design but the mutex), locked instructions (~0.1 per message everywhere except
the mutex's 5.2 / 2.2; shown in 05), memory-ordering flushes (~0.014 per message per thread for every design except
the mutex's ~0.2: the same story as the cross-core panel), L2 misses (the same story as DRAM loads, except the mutex
consumer's 2.8 vs 0.55, noted in 07). All of them are in `flow_counters.csv`.

---

## 4. Setup and method

### Machine

- Lenovo ThinkPad X1 Carbon Gen 8: Intel Core i7-10510U (Comet Lake, 4 cores / 8 threads, 1.8 GHz base, 4.9 GHz
  turbo, 8 MB L3), Ubuntu with Linux 7.0, g++ 15.2, CMake preset `release` (RelWithDebInfo: `-O2 -g`).
- `intel_pstate` with governor `powersave`, turbo on, on AC power, energy-performance preference
  `balance_performance`. Full state of the latency runs in [`env.txt`](env.txt); the flat-out steps ran an hour
  earlier on the same settings. Code: commit `57e4a50` plus the code changes made alongside this report (decoded →
  in book and arrival → in book latency, perf gating in the replay, the direct call in `queue_bench`).
- Producer pinned to CPU 1, consumer to CPU 2 (`LOB_CPUS=1,2`): two separate physical cores. CPU 5 is CPU 1's
  hyperthread sibling and isn't used.
- `perf_event_paranoid` = 0. The NMI watchdog holds one hardware counter, so every perf run uses at most 4 hardware
  events and nothing is multiplexed (`counted_pct` = 100).

### Data

`testdata/cme_mix.mbo.dbn.zst`: Databento MBO (market by order, every order event) for six CME futures over one hour:
1,054,337 records, of which 907,454 change the book (403,988 adds, 349,804 cancels, 153,656 modifies, 6 clears; the
rest are trades and fills, which MBO reports separately from the book changes). Peak 54,202 resting orders. The
script decompresses it once into `benchmarks/linux/databento/data/`. Databento data is licensed: the raw and
decompressed files, the exported flow and `runs/` are git-ignored.

### The flow

```
DBN file ─► Databento SDK decode ─► MboMessage parse ─► [handoff] ─► handle_message ─► 64-slot instrument cache ─► inner book
           └──────────────── producer thread (CPU 1) ────────────┘               └─────────── consumer thread (CPU 2) ───────────┘
```

`databento_replay <file> [label] [print_every] [depth.csv] [levels] [speed] [mode] [book] [batch] [handoff] [latency.csv]`,
e.g. `databento_replay cme_mix.mbo.dbn x 0 "" 40 60 ring ladder-cached64 64 ring-spin lat.csv`.

### Handoffs

| chart label | mode / handoff | what it is |
|---|---|---|
| direct call | `single` / – | one thread decodes and applies each message; no queue, no second thread |
| mutex queue | `mutex` / `mutex-cv` | `std::queue` (a `std::deque`) under a `std::mutex`; the producer locks, pushes, unlocks and signals a condition variable on every push; the consumer sleeps on it when the queue is empty and drains up to 64 messages per wake |
| ring buffer, spin | `ring` / `ring-spin` | lock-free single-producer single-consumer ring (`CircularBuffer`, 65,536 slots, each side caches the other's index); consumer drains up to 64 per wake; when empty it spins on `pause` |
| ring buffer, yield | `ring` / `ring-yield` | the same ring; when empty, `sched_yield()` |
| ring buffer, back-off | `ring` / `ring` | the same ring with `BalancedBackoff`: 256 `pause`s, then 2,000 `sched_yield()`s, then 20 µs sleeps (plus the default 50 µs timer slack) until a message arrives |
| tuned back-off (02 only) | `ring` / `ring-tuned` | `TimedBackoff`, by time instead of counts: spin 500 µs, then 5 µs sleeps with the thread's timer slack lowered to 1 µs (`LOB_BACKOFF=500000,0,5000,1000`, tuned in the earlier final demo) |

On every ring, the producer waits for a free slot with the same strategy when the ring is full.

### Inner books

All three keep aggregate levels (total quantity and order count per price, not the queue of orders), the same order
index (an open-addressing hash table, order ID → price, side, quantity; linear probing, at most half full, doubles
when full) and the same 64-slot instrument cache in front (`OuterBook<…, 64>`). Only the price levels of each side
differ:

| chart label | variant | price levels |
|---|---|---|
| `std::map` | `agg-map-cached64` | `std::map<price, level>`, a red-black tree |
| `std::vector` | `agg-flat-cached64` | sorted `std::vector`, best price at the back; finds a level by checking the 8 nearest the best, then binary search; creating or emptying a level shifts the better levels by one |
| ladder | `ladder-cached64` | an array indexed by (price − base) / tick: finding a level is one subtraction; grows at either end when the price leaves the range |

`book_bench` calls the same books `agg-map`, `agg-flat` and `ladder` (cache included).

### Run modes

- **Flat out** (speed 0): replay the decompressed file as fast as possible. Throughput = records / seconds of the
  replay loop. All counters.
- **Paced** (speed 60): the producer sleeps until each record's receive time, compressed 60x (the hour in a minute).
  Latency, CPU busy, context switches. Pacing keeps the quiet gaps of a real feed, which is what makes the idle
  strategy matter. A sleeping thread wakes up to ~50 µs late (Linux timer slack), so records leave the producer in
  small bursts, much as they arrive off a network card.

### Latency

Three times are recorded for every message, all 1,054,337 per run, with `steady_clock`:

- **push → pickup** (`p50_ns` …): from the producer pushing it (just after decode) until the consumer takes it off
  the queue. The handoff alone. Direct call: 0, there is no handoff. Used in 01 and 02.
- **decoded → in book** (`applied_p50_ns` …): from the push (direct call: from decode) until the book update has
  finished. Handoff plus book update.
- **arrival → in book** (`arrival_p50_ns` …): from the moment the message *arrived* until the book update has
  finished. Arrival is the record's scheduled time in the paced replay, but never earlier than the moment the
  producer last woke up, so the replay's own oversleep is not charged. This is the fair end-to-end number: when a
  thread falls behind in a burst, the messages wait *before* decode, and only this metric sees that wait. That
  matters for the direct call, whose single thread is the slowest (8 M msgs/s): decoded → in book hides its backlog,
  arrival → in book shows it. Used in 06.

Each run's percentiles are computed over all its messages; the charts show the median of 2 runs.

### Counters

- **Per CPU = per thread.** `perf stat -C 1,2 -A` counts each CPU separately; since each thread is pinned, CPU 1 is
  the producer and CPU 2 the consumer. Counts include the kernel's work on that CPU and anything else scheduled there
  (a few % from the desktop).
- **Gated.** The replay and `book_bench` turn counting on and off around the timed loop through perf's control pipe
  (`perf stat -D -1 --control fd:…`, driven from `Core/Source/Core/PerfControl.h` via `LOB_PERF_CTL_FD` /
  `LOB_PERF_ACK_FD`), so file loading, setup and teardown are excluded. 05 is not gated (see 05).
- **Core busy %** = `ref-cycles` / `msr/tsc` per CPU. `ref-cycles` tick at the TSC rate only while the core is
  awake, so this is the share of wall time the core was running something. Spinning and yielding count as busy.
- **Events used:** `cycles`, `instructions`, `branch-misses`, `ref-cycles`, `msr/tsc/`, `context-switches`,
  `mem_load_retired.{l1,l2,l3}_miss`, `cycle_activity.stalls_{mem_any,l3_miss}`,
  `mem_load_l3_hit_retired.xsnp_hitm` (a load found the line modified in another core's cache),
  `mem_inst_retired.lock_loads` (locked read-modify-write instructions), `machine_clears.memory_ordering`, and the
  `tma_*` top-down metrics.

### Repetitions

| step | runs | shown |
|---|---|---|
| handoff counters (05) | 3 | median |
| book only (03 left) | 15 passes | median ns |
| per-op latency (03 right, 04 right) | 3 | median of run percentiles |
| top-down (04) | 6 passes per metric, one metric per run | |
| slow loads (04) | 4 passes, 1 in 199 slow loads sampled | |
| flow counters (07) | 5 per event group | median |
| flat-out throughput (01, 02, 06) | 5, order alternated | median, whiskers min–max |
| paced latency (01, 02, 06) | 2 × 60 s | median of the 2 |

---

## 5. Files and columns

Collected by `tools/linux/databento_report_data.sh`. Step names are the script's `--only` names.

| file | step | what |
|---|---|---|
| `env.txt` | all | machine state when the last invocation started (governor, turbo, power, perf access, commit, steps run) |
| `handoff_counters.csv` | `queue` | 05 |
| `inner_book_variants.csv` | `book` | 03 left, every variant |
| `flow_export.txt` | `book` | the export of the records to a `book_bench` flow: ops, instruments, peak resting orders |
| `inner_book_op_latency.csv` | `book` | 03 right, 04 right |
| `inner_book_counters.csv` | `book` | 04 left |
| `inner_book_slow_loads.csv` | `book` | 04 middle |
| `flow_counters.csv` | `flow` | 07 |
| `flow_stages.csv` | `flow` | 07, stage panel |
| `combos_throughput.csv` | `combos_throughput` | 01 left, 06 left |
| `idle_throughput.csv` | `idle_throughput` | 02 left |
| `combos_latency.csv` | `combos_latency` | 01 right, 06 right |
| `idle_latency.csv` | `idle_latency` | 02 latency and busy panels |
| `RESULTS.md` | `tools/report_tables.py` | every number behind the charts, as tables |
| `runs/` (git-ignored) | all | raw perf outputs, `perf.data` files, replay stdout, per-run latency CSVs and CDFs |

Columns:

- **`handoff_counters.csv`**: `impl` (`direct`, `mutex`, `ring_spin`, `ring_yield`, `ring` = back-off), `run` (0–2),
  `items` (20,000,000), `cpu` (`CPU1`/`CPU2`), `role` (`producer`/`consumer`), `event`, `value` (count over the
  run on that CPU), `counted_pct` (100 = never multiplexed). Per item = value / items.
- **`inner_book_variants.csv`**: `file`, `book` (variant name), `levels`, `index` (order index type), `orders`
  (`aggregate`, `list`, `pooled-fifo`, `lazy-fifo`), `cache` (instrument cache slots), `median_ns` and `min_ns`
  (per record, over 15 passes), `resting_orders` (at the end), `depth_hash` (hash of the final book; equal for every
  variant = they agree).
- **`inner_book_op_latency.csv`**: `scenario`, `impl`, `levels`, `index`, `orders`, `cache`, `run`, `op` (`add`,
  `cancel`, `modify`, `clear`, `all`), `count`, `p50_ns` … `p999_ns`, `max_ns`, `mean_ns`, `clock_overhead_ns`
  (median cost of reading the clock twice; included in the latencies, not subtracted).
- **`inner_book_counters.csv`**: `book`, `kind` (`metric` = a `tma_*` top-down metric, value in % of slots;
  `count` = a raw event, value = total), `name`, `value`, `ops` (book operations in the counted runs, 6 × 907,454).
- **`inner_book_slow_loads.csv`**: `scenario`, `impl`, `source` (file:line of the load), `owner` (class),
  `category` (data structure), `level` (where the data came from: `L1`, `fill buffer`, `L2`, `L3`, `DRAM`),
  `loads` (samples), `latency_cycles` (sum of the sampled loads' latency). 04 regroups `category`: memmove / memcpy /
  vector construction → "shifting & growing arrays", `benchmark harness` → "reading the input".
- **`flow_counters.csv`**: `handler` (`direct`, `mutex-cv`, `spin`, `yield`, `back-off`), `mode`, `handoff`,
  `group` (`basic`, `coherence`, `memory`: events counted together), `run` (0–4), `records`, `cpu`, `role`, `event`,
  `value`, `counted_pct`. Per message = value / records.
- **`flow_stages.csv`**: `handler`, `cpu` (`001`/`002`), `dso`, `space` (`k` kernel, `.` user), `symbol`,
  `overhead_pct` (share of all samples of that handler's recording, both CPUs together).
- **`combos_throughput.csv`**: `handler`, `mode`, `handoff`, `book` (`map`, `vector`, `ladder`), `book_variant`,
  `run` (0–4), `records`, `seconds`, `msgs_per_sec`.
- **`idle_throughput.csv`**: `books` (order books sharing the two cores: 1, 2, 4), `strategy` (`spin`, `yield`,
  `back-off`, `tuned`), `handoff`, `run`, `book_index`, `records`, `seconds`, `msgs_per_sec`. Order book k runs with
  its producer on CPU 1 and consumer on CPU 2 if k is even, the other way round if odd, so with 2 books each core
  runs one book's producer and the other's consumer, and with 4 books two of each.
- **`combos_latency.csv`**: `handler`, `book`, `run`, `book_index`, `cpu_busy_pct` (`CPU1=…;CPU2=…`, ref-cycles /
  TSC over the run), `context_switches` (per CPU, whole run), then the replay's own row: `file`, `mode`, `book`
  (again: the variant name), `handoff`, `batch`, `speed`, `count`, `p50_ns` … `p9999_ns`, `max_ns` (push → pickup),
  `applied_count`, `applied_p50_ns` … `applied_max_ns` (decoded → in book), `arrival_count`, `arrival_p50_ns` …
  `arrival_max_ns` (arrival → in book). The column name `book` appears twice:
  read by position or take the first.
- **`idle_latency.csv`**: `books`, `strategy`, `handoff`, `run`, `book_index`, `cpu_busy_pct`, `context_switches`
  (both for the whole run, all books together, repeated on each book's row), then the replay's row for that book as
  above.

---

## 6. Regenerating

```
tools/linux/build_databento.sh                       # once: the Databento C++ SDK into vendor/
sudo sysctl kernel.perf_event_paranoid=0             # resets on reboot
sudo sysctl kernel.kptr_restrict=0                   # optional: kernel symbols in the stage split
tools/linux/databento_report_data.sh                 # all steps, about 70 min; --only queue,book,flow,... for some
python3 tools/plot_report.py                         # the seven charts into final-demo_images/report/
python3 tools/report_tables.py                       # RESULTS.md
```

Options: `--file` (default `testdata/cme_mix.mbo.dbn.zst`), `--speed` (60), `--runs` (flat-out repetitions, 5),
`--latency-runs` (paced repetitions, 2). `PRIMARY_CPU` / `CROSS_CPU` pick the two cores (default 1 and 2). The
script pauses while the laptop is on battery and records the power state in `env.txt`. Keep the laptop plugged in
and leave it alone during the run (closing the lid is fine if sleep is blocked, e.g. `systemd-inhibit`).

---

## 7. Caveats

- **One laptop.** A 15 W mobile CPU with turbo and `powersave`: clocks move with temperature and load. Rankings and
  ratios are solid (whiskers in the throughput panels show the spread of 5 runs); absolute numbers would differ on a
  server, and absolute queue throughput has moved by up to 2x between sessions on this machine in the past.
- **Power source matters.** On battery the CPU switches to `balance_power`. A first pass of the paced runs happened
  after the charger was unplugged: medians moved by up to 1.7x (the mutex queue with `std::map`: 43 µs on AC, 74 µs
  on battery) and two battery runs of the same setup disagreed by up to 1.5x. Those runs were discarded and redone on
  AC; the script now pauses while on battery.
- **Tails are not comparable.** The file starts with 51,785 snapshot records (4.9% of all) that are replayed
  without pacing, so with a queue they all land in it at once and take milliseconds to drain; the direct call
  applies them as it decodes them. That sets every queued design's p99 (5–21 ms) and is not a property of the
  handoff. At 60x, some live bursts also exceed what any design can apply. Compare at the median (and p90 in 02);
  excluding snapshot records from the latency is the fix for a future run.
- **60x pacing.** Real time would take an hour per run; 60x keeps the quiet gaps that make the idle strategy matter.
- **Idle cores between messages.** In paced runs the threads sleep between bursts and the cores cool down (clock
  ramps down, caches lose data), so a book update there takes ~350 ns instead of ~75 ns book-only. That is real: a
  spinning consumer keeps its core warm, a sleeping one doesn't.
- **Stage split (07) is by function name**: inlined code is counted where it was inlined (see 07).
- **Laptop desktop running.** Per-CPU counts include a few % of desktop activity scheduled on CPUs 1–2.

---

## 8. Older files in this folder

From the first final-demo run (`tools/linux/databento_final_demo.sh`, `tools/linux/databento_oversubscribed.sh`;
charts `../final-demo_images/0*.png`, drawn by `tools/plot_final_demo.py`; written up in the "Final demo" section of
[`benchmarks/linux/README.md`](../../README.md)). Same machine and file, ladder book only:

- `throughput.csv`, `latency.csv`: handoff designs including the mutex-protected ring (`locked-ring`) and sharded
  rings (`sharded2`, `sharded3`: one consumer per instrument group); idle strategies with the consumer's core to itself
  (`none`) and shared with a busy-loop process at nice 0 / nice 19 (`nice0`, `nice19`).
- `idle_sweep.csv`, `idle_sweep.md`: the `TimedBackoff` tuning sweep (spin / yield / sleep / timer slack).
- `oversubscribed_throughput.csv`, `oversubscribed_latency.csv`: 3 or 6 sharded consumers on 4 or 2 cores.

Results from that run that the new charts don't repeat:

- Sharding by instrument: 2 shards, 438 ns median at 60x (vs 545 ns for one ring), 11.2 M msgs/s.
- A busy unrelated process on the consumer's core: `yield` and `BalancedBackoff` wait a whole scheduler time slice
  (~1.5 ms median) because the neighbour takes the core whenever they offer it; the tuned back-off's 5 µs naps with
  1 µs timer slack get 5.7 µs. With other *order books* as neighbours (02 here) the picture flips: yielding hands the
  core to a thread that has messages, and the tuned back-off's 500 µs spin is the problem.
- 6 shards on 2 cores: yield 9.6 M msgs/s, spin 3.8 M msgs/s.
