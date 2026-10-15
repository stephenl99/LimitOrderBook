# Queue benchmark summary

```
date: 2026-10-15T12:59:20-04:00
host: stephen-linder-ThinkPad-X1-Carbon-Gen-8
kernel: 7.0.0-34-generic
cpu: Intel(R) Core(TM) i7-10510U CPU @ 1.80GHz
cpus: 8, smt siblings of cpu1: 1 5 
pinning: primary=1 cross_core=2 smt=5
governor: powersave
no_turbo: 0
compiler: c++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0
commit: 6fdaff2 (dirty)
perf_event_paranoid: 4
```

Buffers, flat out, median M items/s:

| buffer | unpinned | cross_core | smt |
|---|---|---|---|
| ring, uncached counters | 74 | 62 | 105 |
| ring, cached counters | 134 | 141 | 145 |
| ring, per-slot sequence | 218 | 331 | 162 |
| Vyukov MPMC | 56 | 56 | 47 |

Buffers paced at 10,000 msgs/s, median p50 / p99 ns:

| buffer | unpinned | cross_core | smt |
|---|---|---|---|
| ring, uncached counters | 413 / 981 | 395 / 1167 | 393 / 774 |
| ring, cached counters | 385 / 955 | 370 / 1071 | 388 / 769 |
| ring, per-slot sequence | 408 / 1033 | 368 / 1008 | 394 / 806 |
| Vyukov MPMC | 395 / 946 | 390 / 6770 | 407 / 797 |

Buffers paced at 100,000 msgs/s, median p50 / p99 ns:

| buffer | unpinned | cross_core | smt |
|---|---|---|---|
| ring, uncached counters | 215 / 388 | 216 / 6064 | 77 / 114 |
| ring, cached counters | 174 / 3915 | 176 / 6921 | 79 / 110 |
| ring, per-slot sequence | 176 / 4437 | 189 / 8157 | 81 / 169 |
| Vyukov MPMC | 187 / 7122 | 192 / 7032 | 93 / 168 |

Buffers paced at 1,000,000 msgs/s, median p50 / p99 ns:

| buffer | unpinned | cross_core | smt |
|---|---|---|---|
| ring, uncached counters | 178 / 683 | 167 / 2353 | 71 / 107 |
| ring, cached counters | 155 / 3489 | 125 / 2460 | 74 / 113 |
| ring, per-slot sequence | 181 / 3479 | 154 / 6623 | 73 / 115 |
| Vyukov MPMC | 187 / 347 | 159 / 640 | 80 / 115 |
