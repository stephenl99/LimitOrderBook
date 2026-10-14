#!/usr/bin/env python3
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

COLORS = {"mutex": "#2a78d6", "ring": "#eb6834", "ring_spin": "#1baf7a", "ring_backoff": "#e87ba4", "single": "#52514e"}
NAMES = {
    "mutex": "mutex + condvar",
    "ring": "ring, yield when idle",
    "ring_spin": "ring, spin when idle",
    "ring_backoff": "ring, spin then back off",
    "single": "single thread, no queue",
}
INK = "#0b0b0b"
MUTED = "#52514e"
GRID = "#e4e3de"
PERCENTILES = [("p50_ns", "p50"), ("p99_ns", "p99"), ("p999_ns", "p99.9")]


def short(n):
    return f"{n / 1e6:.2g}M" if n >= 1_000_000 else f"{n / 1000:.0f}k"


def duration(ns):
    if ns >= 1e6:
        return f"{ns / 1e6:.1f} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:.1f} µs"
    return f"{ns:.0f} ns"


def load_throughput(path):
    rows = defaultdict(list)
    with open(path) as f:
        for row in csv.DictReader(f):
            size = int(row["size"])
            per_sec = size / (float(row["ms"]) / 1000.0) / 1e6
            rows[(row["bench"], row["impl"], size)].append(per_sec)
    return rows


def load_latency(path):
    rows = defaultdict(lambda: defaultdict(list))
    with open(path) as f:
        for row in csv.DictReader(f):
            for column, _ in PERCENTILES:
                rows[(row["impl"], int(row["rate"]))][column].append(float(row[column]))
    return rows


def style(ax):
    ax.spines[["top", "right"]].set_visible(False)
    ax.spines[["left", "bottom"]].set_color(MUTED)
    ax.tick_params(colors=MUTED, labelsize=9)
    ax.yaxis.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def handoff(rows, out):
    impls = ["mutex", "ring", "ring_spin", "ring_backoff"]
    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    size = next(s for (b, _, s) in rows if b == "queue")
    for x, impl in enumerate(impls):
        values = rows[("queue", impl, size)]
        jitter = [x + (i % 5 - 2) * 0.04 for i in range(len(values))]
        ax.scatter(jitter, values, s=28, color=COLORS[impl], alpha=0.75, linewidths=0)
        median = statistics.median(values)
        ax.hlines(median, x - 0.25, x + 0.25, color=INK, linewidth=1.5)
        ax.annotate(f"{median:.0f}M/s", (x + 0.28, median), va="center", fontsize=9, color=INK)

    ax.set_xticks(range(len(impls)), [NAMES[i].replace(", ", ",\n") for i in impls], color=INK, fontsize=9)
    ax.set_xlim(-0.6, len(impls) - 0.2)
    ax.set_ylim(bottom=0)
    ax.set_ylabel("items handed off per second (millions)", color=MUTED, fontsize=9)
    ax.set_title(f"Queue only, flat out: {short(size)} int64 between two threads", loc="left", fontsize=11, color=INK)
    style(ax)
    fig.tight_layout()
    fig.savefig(out / "queue_handoff.png", dpi=160)
    plt.close(fig)


def decode(rows, out):
    fig, ax = plt.subplots(figsize=(6.4, 4))
    sizes = sorted({s for (b, _, s) in rows if b == "decode"})
    for impl in ["single", "mutex", "ring"]:
        medians = [statistics.median(rows[("decode", impl, s)]) for s in sizes]
        lows = [min(rows[("decode", impl, s)]) for s in sizes]
        highs = [max(rows[("decode", impl, s)]) for s in sizes]
        dashed = impl == "single"
        ax.fill_between(sizes, lows, highs, color=COLORS[impl], alpha=0.12, linewidth=0)
        ax.plot(sizes, medians, color=COLORS[impl], linewidth=2, marker="o", markersize=5,
                linestyle="--" if dashed else "-", label=NAMES[impl])

    ax.set_xscale("log")
    ax.set_xticks(sizes, [short(s) for s in sizes])
    ax.minorticks_off()
    ax.set_ylim(bottom=0)
    ax.set_xlabel("messages in file", color=MUTED, fontsize=9)
    ax.set_ylabel("messages decoded per second (millions)", color=MUTED, fontsize=9)
    ax.set_title("Full ITCH decode into the book", loc="left", fontsize=11, color=INK)
    ax.legend(frameon=False, fontsize=9, loc="lower left")
    ax.text(0.0, -0.2, "line = median, band = min to max across runs", transform=ax.transAxes, fontsize=8, color=MUTED)
    style(ax)
    fig.tight_layout()
    fig.savefig(out / "decode_throughput.png", dpi=160)
    plt.close(fig)


def latency(rows, out):
    impls = ["mutex", "ring", "ring_spin", "ring_backoff"]
    rates = sorted({rate for (_, rate) in rows})
    fig, axes = plt.subplots(1, len(rates), figsize=(9.6, 4), sharey=True)
    xs = range(len(PERCENTILES))
    for ax, rate in zip(axes, rates):
        for impl in impls:
            series = rows[(impl, rate)]
            medians = [statistics.median(series[c]) for c, _ in PERCENTILES]
            lows = [statistics.quantiles(series[c], n=4)[0] for c, _ in PERCENTILES]
            highs = [statistics.quantiles(series[c], n=4)[2] for c, _ in PERCENTILES]
            ax.fill_between(xs, lows, highs, color=COLORS[impl], alpha=0.15, linewidth=0)
            ax.plot(xs, medians, color=COLORS[impl], linewidth=2, marker="o", markersize=5, label=NAMES[impl])
        ax.set_yscale("log")
        ax.set_xticks(xs, [label for _, label in PERCENTILES], color=INK, fontsize=9)
        ax.set_xlim(-0.3, len(PERCENTILES) - 0.7)
        ax.set_title(f"{short(rate)} msgs/s", loc="left", fontsize=10, color=INK)
        style(ax)
    ticks = [100, 1e3, 1e4, 1e5, 1e6]
    axes[0].set_yticks(ticks, ["100 ns", "1 µs", "10 µs", "100 µs", "1 ms"])
    axes[0].minorticks_off()
    axes[0].set_ylabel("push to pop latency (log scale)", color=MUTED, fontsize=9)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, frameon=False, fontsize=9, ncol=4, loc="lower center", bbox_to_anchor=(0.5, 0.0))
    fig.suptitle("Handoff latency at a fixed send rate", x=0.065, y=0.975, ha="left", fontsize=11, color=INK)
    fig.text(0.065, 0.905, "line = median across runs, band = middle half of runs", fontsize=8, color=MUTED)
    fig.tight_layout(rect=(0, 0.07, 1, 0.89))
    fig.savefig(out / "handoff_latency.png", dpi=160)
    plt.close(fig)


def summary(throughput, lat, out):
    lines = ["bench,size_or_rate,impl,metric,value"]
    for bench, impl, size in sorted(throughput):
        median = statistics.median(throughput[(bench, impl, size)])
        lines.append(f"{bench},{size},{impl},median_M_per_s,{median:.2f}")
    for impl, rate in sorted(lat, key=lambda k: (k[1], k[0])):
        for column, label in PERCENTILES:
            median = statistics.median(lat[(impl, rate)][column])
            lines.append(f"latency,{rate},{impl},{label}_ns,{median:.0f}")
    (out / "summary.csv").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


def main():
    throughput = load_throughput(sys.argv[1])
    lat = load_latency(sys.argv[2])
    out = Path(sys.argv[3])
    out.mkdir(parents=True, exist_ok=True)
    handoff(throughput, out)
    decode(throughput, out)
    latency(lat, out)
    summary(throughput, lat, out)


if __name__ == "__main__":
    main()
