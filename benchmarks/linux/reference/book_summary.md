# Book benchmark summary

```
date: 2026-10-15T12:55:21-04:00
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

Median ns per operation (each op includes one best bid/ask read):

| impl | equity | thin | deep | volatile | wide | cme6 | multi | random_ids |
|---|---|---|---|---|---|---|---|---|
| pooled-ladder | 45.5 | 38.8 | 41.1 | 40.5 | 46.1 | 48.1 | 115.9 | 48.7 |
| ladder | 48.3 | 42.4 | 45.9 | 42.6 | 49.7 | 53.6 | 107.4 | 51.4 |
| ladder-cache1 | 47.2 | 42.0 | 42.5 | 43.3 | 50.1 | 59.6 | 113.7 | 51.0 |
| pooled-hybrid | 48.2 | 41.9 | 45.9 | 41.0 | 54.6 | 54.5 | 114.6 | 52.8 |
| agg-flat | 57.5 | 50.6 | 48.2 | 42.7 | 61.8 | 57.9 | 114.2 | 59.1 |
| ladder-nocache | 59.7 | 52.4 | 56.6 | 55.0 | 60.7 | 60.0 | 115.7 | 63.0 |
| ladder-identity | 62.5 | 50.6 | 194.8 | 62.1 | 63.6 | 52.2 | 109.3 | 52.2 |
| agg | 103.4 | 95.7 | 93.2 | 94.6 | 114.6 | 110.0 | 225.3 | 111.0 |
| tree | 134.8 | 110.3 | 126.5 | 124.6 | 138.0 | 151.2 | 353.5 | 138.4 |
| flat | 137.8 | 109.9 | 135.1 | 127.2 | 147.1 | 146.8 | 321.7 | 127.1 |
| hybrid | 136.3 | 111.1 | 129.4 | 125.2 | 143.6 | 149.6 | 322.7 | 137.5 |
| tree-nocache | 150.0 | 116.0 | 139.4 | 141.7 | 160.6 | 159.0 | 373.9 | 162.3 |
