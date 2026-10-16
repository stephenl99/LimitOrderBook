# Handler x inner book rankings

## Throughput, best first (M msgs/s)

| rank | handler | book | M msgs/s |
|---|---|---|---|
| 1 | ring | pooled-ladder | 11.42 |
| 2 | ring-spin | pooled-ladder | 11.30 |
| 3 | ring-yield | pooled-ladder | 11.18 |
| 4 | sequenced | pooled-ladder | 11.12 |
| 5 | ring | ladder | 10.74 |
| 6 | sharded3 | hybrid | 10.65 |
| 7 | ring-spin | pooled-hybrid | 10.61 |
| 8 | ring | pooled-hybrid | 10.60 |
| 9 | ring-yield | agg-flat | 10.43 |
| 10 | ring-yield | pooled-hybrid | 10.43 |
| 11 | ring-yield | ladder | 10.29 |
| 12 | ring | agg-flat | 10.24 |
| 13 | sequenced | ladder | 10.19 |
| 14 | ring-spin | ladder | 10.13 |
| 15 | ring-spin | agg-flat | 10.07 |
| 16 | sharded2 | agg-flat | 10.05 |
| 17 | sharded3 | tree | 10.03 |
| 18 | sharded3 | lazy-handle-ladder | 10.01 |
| 19 | sequenced | pooled-hybrid | 10.00 |
| 20 | sharded2 | pooled-hybrid | 9.92 |
| 21 | sharded2 | lazy-handle-ladder | 9.90 |
| 22 | sharded2 | ladder | 9.66 |
| 23 | sharded2 | pooled-ladder | 9.61 |
| 24 | sharded2 | hybrid | 9.60 |
| 25 | sharded3 | agg-flat | 9.54 |
| 26 | sequenced | agg-flat | 9.45 |
| 27 | sharded3 | pooled-ladder | 9.35 |
| 28 | sharded3 | ladder | 9.22 |
| 29 | sharded3 | pooled-hybrid | 8.75 |
| 30 | sharded2 | tree | 8.24 |
| 31 | ring-yield | lazy-handle-ladder | 7.92 |
| 32 | ring | lazy-handle-ladder | 7.91 |
| 33 | ring-spin | lazy-handle-ladder | 7.63 |
| 34 | sequenced | lazy-handle-ladder | 7.52 |
| 35 | single | pooled-ladder | 6.89 |
| 36 | single | ladder | 6.61 |
| 37 | single | pooled-hybrid | 6.35 |
| 38 | single | agg-flat | 6.25 |
| 39 | locked-ring | lazy-handle-ladder | 6.01 |
| 40 | ring-yield | hybrid | 5.47 |
| 41 | single | lazy-handle-ladder | 5.35 |
| 42 | ring | hybrid | 5.19 |
| 43 | locked-ring | agg-flat | 5.18 |
| 44 | locked-ring | pooled-hybrid | 5.14 |
| 45 | ring-spin | hybrid | 4.87 |
| 46 | ring | tree | 4.69 |
| 47 | sequenced | hybrid | 4.59 |
| 48 | ring-yield | tree | 4.53 |
| 49 | ring-spin | tree | 4.52 |
| 50 | locked-ring | hybrid | 4.43 |
| 51 | single | hybrid | 4.31 |
| 52 | mutex-cv | hybrid | 4.27 |
| 53 | locked-ring | pooled-ladder | 4.18 |
| 54 | sequenced | tree | 4.13 |
| 55 | locked-ring | ladder | 4.06 |
| 56 | locked-ring | tree | 3.74 |
| 57 | mutex-cv | tree | 3.52 |
| 58 | single | tree | 3.36 |
| 59 | mutex-cv | lazy-handle-ladder | 2.78 |
| 60 | mutex-cv | pooled-ladder | 2.49 |
| 61 | mutex-cv | ladder | 2.46 |
| 62 | mutex-cv | pooled-hybrid | 2.43 |
| 63 | mutex-cv | agg-flat | 2.39 |

## Latency, best median first

| rank | queue | book | median | p90 | p99 | p99.9 |
|---|---|---|---|---|---|---|
| 1 | ring-spin | agg-flat | 262 ns | 1.6 µs | 786.3 µs | 822.9 µs |
| 2 | ring-spin | ladder | 269 ns | 1.4 µs | 1.12 ms | 1.20 ms |
| 3 | ring-spin | lazy-handle-ladder | 273 ns | 2.5 µs | 1.66 ms | 1.71 ms |
| 4 | ring-spin | pooled-hybrid | 281 ns | 2.1 µs | 805.0 µs | 862.5 µs |
| 5 | ring-spin | pooled-ladder | 281 ns | 1.9 µs | 881.2 µs | 885.7 µs |
| 6 | ring-yield | ladder | 422 ns | 1.5 µs | 2.82 ms | 3.24 ms |
| 7 | ring-yield | agg-flat | 424 ns | 1.8 µs | 2.36 ms | 3.12 ms |
| 8 | ring-yield | pooled-hybrid | 437 ns | 2.1 µs | 2.51 ms | 3.56 ms |
| 9 | ring-yield | pooled-ladder | 438 ns | 1.9 µs | 2.18 ms | 2.61 ms |
| 10 | ring-yield | lazy-handle-ladder | 444 ns | 2.8 µs | 4.11 ms | 4.72 ms |
| 11 | ring-spin | hybrid | 490 ns | 5.2 µs | 1.53 ms | 1.81 ms |
| 12 | ring-yield | hybrid | 603 ns | 5.2 µs | 4.48 ms | 5.24 ms |
| 13 | ring-spin | tree | 853 ns | 10.6 µs | 1.70 ms | 1.97 ms |
| 14 | sequenced | ladder | 985 ns | 63.5 µs | 3.17 ms | 3.87 ms |
| 15 | ring-yield | tree | 991 ns | 13.7 µs | 3.98 ms | 4.56 ms |
| 16 | sequenced | pooled-ladder | 1.0 µs | 64.7 µs | 2.18 ms | 2.73 ms |
| 17 | sequenced | agg-flat | 1.0 µs | 65.5 µs | 2.31 ms | 3.20 ms |
| 18 | sequenced | pooled-hybrid | 1.1 µs | 64.4 µs | 2.26 ms | 3.14 ms |
| 19 | ring | ladder | 1.1 µs | 65.8 µs | 1.17 ms | 1.68 ms |
| 20 | ring | pooled-ladder | 1.1 µs | 66.3 µs | 956.0 µs | 2.18 ms |
| 21 | ring | agg-flat | 1.1 µs | 65.9 µs | 940.3 µs | 1.53 ms |
| 22 | sequenced | lazy-handle-ladder | 1.1 µs | 65.6 µs | 5.01 ms | 5.93 ms |
| 23 | ring | lazy-handle-ladder | 1.1 µs | 65.0 µs | 1.72 ms | 1.76 ms |
| 24 | ring | pooled-hybrid | 1.2 µs | 65.1 µs | 828.3 µs | 2.21 ms |
| 25 | sequenced | hybrid | 1.9 µs | 67.2 µs | 4.47 ms | 5.16 ms |
| 26 | locked-ring | lazy-handle-ladder | 2.0 µs | 63.6 µs | 3.87 ms | 4.37 ms |
| 27 | locked-ring | agg-flat | 2.0 µs | 63.0 µs | 1.03 ms | 1.31 ms |
| 28 | ring | hybrid | 2.0 µs | 68.5 µs | 1.57 ms | 2.57 ms |
| 29 | locked-ring | pooled-ladder | 2.1 µs | 64.2 µs | 1.44 ms | 1.98 ms |
| 30 | locked-ring | ladder | 2.1 µs | 63.3 µs | 2.33 ms | 2.56 ms |
| 31 | locked-ring | pooled-hybrid | 2.2 µs | 63.6 µs | 1.15 ms | 2.61 ms |
| 32 | sequenced | tree | 2.2 µs | 69.2 µs | 4.29 ms | 4.89 ms |
| 33 | ring | tree | 2.9 µs | 72.1 µs | 2.35 ms | 2.66 ms |
| 34 | locked-ring | hybrid | 3.0 µs | 65.7 µs | 2.74 ms | 3.08 ms |
| 35 | locked-ring | tree | 3.3 µs | 71.6 µs | 2.60 ms | 2.93 ms |
| 36 | mutex-cv | pooled-hybrid | 72.5 µs | 91.4 µs | 985.5 µs | 2.99 ms |
| 37 | mutex-cv | ladder | 73.0 µs | 91.6 µs | 2.29 ms | 2.91 ms |
| 38 | mutex-cv | agg-flat | 73.1 µs | 90.7 µs | 1.05 ms | 1.86 ms |
| 39 | mutex-cv | pooled-ladder | 73.2 µs | 92.8 µs | 1.28 ms | 2.27 ms |
| 40 | mutex-cv | lazy-handle-ladder | 73.2 µs | 93.1 µs | 3.02 ms | 3.57 ms |
| 41 | mutex-cv | tree | 75.9 µs | 127.1 µs | 1.42 ms | 2.81 ms |
| 42 | mutex-cv | hybrid | 76.7 µs | 100.1 µs | 3.32 ms | 3.48 ms |

## Best trade-offs (nothing beats them on both throughput and median latency)

| handler | book | M msgs/s | median |
|---|---|---|---|
| ring-spin | agg-flat | 10.07 | 262 ns |
| ring-spin | ladder | 10.13 | 269 ns |
| ring-spin | pooled-hybrid | 10.61 | 281 ns |
| ring-spin | pooled-ladder | 11.30 | 281 ns |
| ring | pooled-ladder | 11.42 | 1.1 µs |
