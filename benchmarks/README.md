# queue benchmarks

Comparing ways to hand messages from the parser thread to the handler thread:

- **mutex + condvar** — the original, `std::queue` behind a mutex. Kept in `Core/Source/Baseline/MutexDecoder.cpp`.
- **ring, yield when idle** — the lock-free SPSC ring in `Core/Source/Core/CircularBuffer.h`, calling `std::this_thread::yield()` when full/empty. This is what `Decoder` uses.
- **ring, spin when idle** — same ring, CPU pause instruction instead of `yield()`.
- **single thread** — no queue at all (`Core/Source/Baseline/SingleThreadDecoder.cpp`). Only in the decode test.

File mapping, framing, `itch::decode_message`, `itch::handle_message` and the book are shared, so the handoff is the only difference.

M2 Max, macOS 14.6.1, Apple clang 16, `-O2`. Order of variants rotates every run. Benchmark-owned threads run at
`QOS_CLASS_USER_INTERACTIVE` (new threads get the default class otherwise and can land on efficiency cores).

```
make queue_report
```

regenerates fixtures, reruns everything and redraws the plots. Takes about 3 minutes.

## Full decode

| messages | single thread | mutex | ring, yield |
|---|---|---|---|
| 15k | 11.02 M/s | 5.70 | 7.60 |
| 150k | 10.89 M/s | 5.75 | 7.54 |
| 1.5M | 10.06 M/s | 5.36 | 6.90 |

![decode throughput](decode_throughput.png)

The ring is ~30% ahead of the mutex end to end. It wasn't always: the first version of this table had all three
around 4 M/s and the ring within noise of the mutex, because every message was paying for things that had nothing to
do with the queue. Three changes moved it (150k / 1.5M messages, alternating runs against the previous binary):

| change | single | mutex | ring |
|---|---|---|---|
| mmap the file, decode from spans (was: `ifstream` + a `std::vector` per frame) | +59% / +35% | +42% / +14% | +49% / +15% |
| `Order` stored by value in the level list (was: `unique_ptr<Order>` in the list) | +10% / +62% | -1% / +22% | +10% / +40% |
| list and order-index nodes from a per-book `pmr` pool | +16% / +18% | +7% / +11% | +2% / +12% |
| all three | 2.0x / 2.6x | +53% / +55% | +66% / +82% |

Once the per-message cost dropped from ~250 ns to ~140 ns (ring) the handoff became a big enough share of it to see.

One thread is still the fastest way to replay a file, by a lot. The two-thread versions allocate each message on the
parser thread and free it on the handler thread, and add a handoff, and parse + apply are both short. The pipeline is
for a live feed where the network read can't wait on the book; there the number that matters is latency, below.

All three decoders produce the same book on the 1.5M file (same resting orders and depth per level on all 8 locates).

## Handoff latency

Producer sends at a fixed rate, stamps each item with its scheduled send time, consumer records `now - stamp`.
2 seconds per run, 7 runs. Median across runs:

| rate | variant | p50 | p99 | p99.9 |
|---|---|---|---|---|
| 10k/s | mutex | 5.79 µs | 16.7 µs | 1.2 ms |
| | ring, yield | 167 ns | 3.6 µs | 12.3 µs |
| | ring, spin | 84 ns | 3.2 µs | 8.8 µs |
| 100k/s | mutex | 5.88 µs | 9.4 µs | 18.2 µs |
| | ring, yield | 166 ns | 3.2 µs | 12.4 µs |
| | ring, spin | 83 ns | 1.4 µs | 8.9 µs |
| 1M/s | mutex | 1.54 µs | 9.8 µs | 22.8 µs |
| | ring, yield | 167 ns | 3.3 µs | 12.5 µs |
| | ring, spin | 83 ns | 2.6 µs | 9.0 µs |

![handoff latency](handoff_latency.png)

With the mutex the consumer is asleep on the condition variable most of the time and every message pays a wakeup,
~6 µs. The ring's consumer is already running, so the median is 35x (yield) to 70x (spin) lower. At 1M/s the mutex
median drops to 1.5 µs because the consumer rarely gets a chance to go to sleep.

p99.9 moves a lot between sessions (an earlier run had yield at 600 µs at 10k/s). It's mostly the OS. The medians and
p99s have been stable across runs.

Clock resolution here is ~42 ns (mach ticks), so 83 and 167 are 2 and 4 ticks.

## Queue only, flat out

5M `int64_t`, producer pushes as fast as it can, 15 runs:

| variant | median |
|---|---|
| mutex | 21.3 M/s |
| ring, yield | 42.2 M/s |
| ring, spin | 25.2 M/s |

![queue handoff](queue_handoff.png)

Yield is 2x the mutex. Spin is barely faster than the mutex, which surprised me: the side that's waiting keeps
reloading the other side's counter in a tight loop, so the two cores fight over that cache line. Yielding backs off and
lets the other side run. So the idle strategy is a real tradeoff: spin for latency, yield for throughput. Caching the
other side's counter locally should fix most of it (not done yet).

## Real data

One hour of CME ESZ6 MBO from Databento (163,149 records), replayed single-threaded with `make replay`, zstd
decompression included:

| | median |
|---|---|
| six message subclasses on the heap, bytes copied into a vector | 4.43 M/s |
| one message class by value | 5.72 M/s |
| decoded from a span, no copy | 6.42 M/s |
| `Order` by value in the list | 7.70 M/s |
| pooled list and index nodes | 9.96 M/s |

Each step measured against the one before in alternating runs. The book printed after every one of the 134,206
completed events is identical at every step.

### Single thread vs ring pipeline on the real data

`make replay_compare` — same file, same decoder and book, 25 alternating runs each, results in
`replay_threading.csv`:

| | median | middle half of runs |
|---|---|---|
| single thread | 9.2–9.5 M/s | 9.0–9.6 |
| ring pipeline (`MessagePipeline<MboMessage, 65536>`) | 11.0–12.3 M/s | 9.8–13.3 |

Two sessions, ring / single = 1.20x and 1.28x. The ring version is noisier.

This is the opposite of the ITCH result above, where one thread beats the pipeline, and the difference is what goes
through the ring. ITCH pushes a `unique_ptr<Message>`: allocated on the parser thread, freed on the handler thread.
Databento pushes an `MboMessage` by value, about 70 bytes, no allocation at all. The producer side also has real work
to overlap with the book (zstd decompression and the SDK's record decoding).

The book printed after every completed event is identical in both modes, and TSan is clean on the ring replay.
Caveat: it's one quiet hour, 163k records, about 15 ms of work, so thread start-up is a visible part of each run.

## Fixtures

ITCH: `gen_big_mix.py --locates 8 --mutate-every 4`, adds over 8 symbols with an execute + cancel every 4th add.
Synthetic. Still need a real NASDAQ sample.

## Next

- messages by value in the ITCH ring (a `variant`) instead of `unique_ptr<Message>` — the last per-message allocation on that path, and the one that crosses threads. The Databento pipeline already does this and it's the one case where two threads beat one
- cache the opposite counter in the ring, rerun spin vs yield
- flat price levels near the top of book instead of `std::map`
- real NASDAQ data
