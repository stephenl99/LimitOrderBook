#!/usr/bin/env python3
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

INK = "#0b0b0b"
MUTED = "#52514e"

PANELS = [
    ("flat", "throughput_mps", "Flat-out throughput (M/s, higher is better)", "mps"),
    ("1000000", "p99_ns", "p99 latency at 1M msgs/s", "ns"),
    ("10000", "p99_ns", "p99 latency at 10k msgs/s", "ns"),
    ("1000", "p50_ns", "Median latency at 1k msgs/s", "ns"),
    ("10000", "consumer_cpu_pct", "Consumer CPU at 10k msgs/s (%)", "pct"),
    ("1000", "consumer_cpu_pct", "Consumer CPU at 1k msgs/s (%)", "pct"),
]


def load(path):
    rows = defaultdict(list)
    with open(path) as f:
        for row in csv.DictReader(f):
            key = (row["policy"], row["pause"], int(row["spin"]), int(row["yield"]), row["scenario"])
            rows[key].append(row)
    return rows


def median(rows, key, column):
    values = [float(r[column]) for r in rows.get(key, []) if r[column] != ""]
    return statistics.median(values) if values else float("nan")


def fmt(value, kind):
    if np.isnan(value):
        return ""
    if kind == "mps":
        return f"{value:.0f}"
    if kind == "pct":
        return f"{value:.0f}%"
    if value >= 1e6:
        return f"{value / 1e6:.1f}ms"
    if value >= 1e3:
        return f"{value / 1e3:.1f}µs"
    return f"{value:.0f}ns"


def heatmaps(rows, out):
    spins = sorted({k[2] for k in rows if k[0] == "backoff" and k[1] == "yield"})
    yields = sorted({k[3] for k in rows if k[0] == "backoff" and k[1] == "yield"})
    fig, axes = plt.subplots(2, 3, figsize=(13, 7.4))
    for ax, (scenario, column, title, kind) in zip(axes.flat, PANELS):
        grid = np.array([[median(rows, ("backoff", "yield", s, y, scenario), column) for s in spins] for y in yields])
        shown = np.log10(grid) if kind == "ns" else grid
        ax.imshow(shown, cmap="Blues", aspect="auto", origin="lower")
        lo, hi = np.nanmin(shown), np.nanmax(shown)
        for i in range(len(yields)):
            for j in range(len(spins)):
                dark = (shown[i, j] - lo) / (hi - lo + 1e-12) > 0.55
                ax.text(j, i, fmt(grid[i, j], kind), ha="center", va="center", fontsize=8.5,
                        color="white" if dark else INK)
        ax.set_xticks(range(len(spins)), [str(s) for s in spins], fontsize=8.5, color=MUTED)
        ax.set_yticks(range(len(yields)), [str(y) for y in yields], fontsize=8.5, color=MUTED)
        ax.set_xlabel("spin attempts", color=MUTED, fontsize=8.5)
        ax.set_ylabel("yield attempts", color=MUTED, fontsize=8.5)
        ax.set_title(title, loc="left", fontsize=10, color=INK)
        for side in ax.spines.values():
            side.set_visible(False)
    fig.suptitle("Back-off tuning: spin attempts x yield attempts (median of runs, pause = yield instruction)",
                 x=0.01, ha="left", fontsize=11, color=INK)
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    fig.savefig(out / "backoff_sweep.png", dpi=150)
    plt.close(fig)


def summary(rows, out):
    keys = sorted({k[:4] for k in rows})
    header = ["policy", "pause", "spin", "yield", "flat_mps",
              "p50_1M_ns", "p99_1M_ns", "p99_100k_ns", "p99_10k_ns", "p999_10k_ns", "p50_1k_ns", "p99_1k_ns",
              "cpu_100k_pct", "cpu_10k_pct", "cpu_1k_pct"]
    lines = [",".join(header)]
    for policy, pause, spin, yld in keys:
        k = (policy, pause, spin, yld)
        values = [
            median(rows, k + ("flat",), "throughput_mps"),
            median(rows, k + ("1000000",), "p50_ns"),
            median(rows, k + ("1000000",), "p99_ns"),
            median(rows, k + ("100000",), "p99_ns"),
            median(rows, k + ("10000",), "p99_ns"),
            median(rows, k + ("10000",), "p999_ns"),
            median(rows, k + ("1000",), "p50_ns"),
            median(rows, k + ("1000",), "p99_ns"),
            median(rows, k + ("100000",), "consumer_cpu_pct"),
            median(rows, k + ("10000",), "consumer_cpu_pct"),
            median(rows, k + ("1000",), "consumer_cpu_pct"),
        ]
        lines.append(",".join([policy, pause, str(spin), str(yld)] + [f"{v:.1f}" for v in values]))
    (out / "backoff_sweep_summary.csv").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


def main():
    rows = load(sys.argv[1])
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    heatmaps(rows, out)
    summary(rows, out)


if __name__ == "__main__":
    main()
