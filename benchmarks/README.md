# queue benchmarks

Comparing ways to hand messages from the parser thread to the handler thread:

- **mutex + condvar** — the original, `std::queue` behind a mutex. Kept in `Core/Source/Baseline/MutexDecoder.cpp`.
- **ring, yield when idle** — the lock-free SPSC ring in `Core/Source/Core/CircularBuffer.h`, calling `std::this_thread::yield()` when full/empty. This is what `Decoder` uses.
- **ring, spin when idle** — same ring, CPU pause instruction instead of `yield()`.
- **single thread** — no queue at all (`Core/Source/Baseline/SingleThreadDecoder.cpp`). Only in the decode test.

Framing, `itch::decode_message`, `itch::handle_message` and the book are shared, so the handoff is the only difference.

M2 Max, macOS 14.6.1, Apple clang 16, `-O2`. Order of variants rotates every run. Benchmark-owned threads run at
`QOS_CLASS_USER_INTERACTIVE` (new threads get the default class otherwise and can land on efficiency cores).

```
make queue_report
```

regenerates fixtures, reruns everything and redraws the plots. Takes about 3 minutes.

## Handoff latency

Producer sends at a fixed rate, stamps each item with its scheduled send time, consumer records `now - stamp`.
2 seconds per run, 7 runs. Median across runs:

| rate | variant | p50 | p99 | p99.9 |
|---|---|---|---|---|
| 10k/s | mutex | 5.96 µs | 18.8 µs | 648 µs |
| | ring, yield | 167 ns | 8.6 µs | 604 µs |
| | ring, spin | 84 ns | 3.2 µs | 11.6 µs |
| 100k/s | mutex | 5.96 µs | 9.9 µs | 26.4 µs |
| | ring, yield | 167 ns | 5.4 µs | 85.5 µs |
| | ring, spin | 84 ns | 3.1 µs | 34.7 µs |
| 1M/s | mutex | 1.54 µs | 10.7 µs | 43.0 µs |
| | ring, yield | 167 ns | 4.8 µs | 21.2 µs |
| | ring, spin | 83 ns | 2.8 µs | 9.0 µs |

![handoff latency](handoff_latency.png)

This is where the ring wins. With the mutex the consumer is asleep on the condition variable most of the time and
every message pays a wakeup, ~6 µs. The ring's consumer is already running, so the median is 35x (yield) to 70x (spin)
lower. At 1M/s the mutex median drops to 1.5 µs because the consumer rarely gets a chance to go to sleep.

p99.9 is mostly the OS, not the queue — the bands overlap and one preempted run moves it by 10x. I wouldn't claim
anything from that column except that spinning is the most stable.

Clock resolution here is ~42 ns (mach ticks), so 83 and 167 are 2 and 4 ticks.

## Queue only, flat out

5M `int64_t`, producer pushes as fast as it can, 15 runs:

| variant | median |
|---|---|
| mutex | 21.8 M/s |
| ring, yield | 36.5 M/s (27–47 across runs) |
| ring, spin | 22.0 M/s |

![queue handoff](queue_handoff.png)

Yield is 1.7x the mutex but noisy. Spin is no faster than the mutex here, which surprised me: the side that's waiting
keeps reloading the other side's counter in a tight loop, so the two cores fight over that cache line. Yielding backs
off and lets the other side run. So the idle strategy is a real tradeoff: spin for latency, yield for throughput.
Caching the other side's counter locally should fix most of it (not done yet).

## Full decode

| messages | single thread | mutex | ring, yield |
|---|---|---|---|
| 15k | 6.09 M/s | 3.98 | 4.11 |
| 150k | 5.85 M/s | 4.37 | 4.68 |
| 1.5M | 4.11 M/s | 3.95 | 3.67 |

![decode throughput](decode_throughput.png)

Two things:

1. Mutex vs ring barely matters. At ~4M msgs/s a message costs ~250 ns and the handoff is a small part of it.
2. One thread beats both two-thread versions on file replay, by ~40% on the smaller files. Every message is allocated
   on the parser thread and freed on the handler thread, plus the handoff, and that costs more than running parse and
   apply in parallel saves. At 1.5M messages the book (1M resting orders) dominates and all three converge.

So for replaying a file the pipeline is the wrong shape. It's the right shape for a live feed, where the network read
can't wait on the book, and there the number that matters is the latency table above.

All three decoders produce the same book on the 1.5M file (same resting orders and depth per level
on all 8 locates).

## Fixtures

`gen_big_mix.py --locates 8 --mutate-every 4`: adds over 8 symbols with an execute + cancel every 4th add. Synthetic.
Still need to run this on a real NASDAQ sample.

## Next

- cache the opposite counter in the ring, rerun spin vs yield
- stop allocating per message (decode from a span, messages by value in the ring, pooled orders), then rerun decode
- real data
