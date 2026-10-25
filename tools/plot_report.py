#!/usr/bin/env python3
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker

INK, MUTED, GRID, SURFACE, GRAY = "#0b0b0b", "#52514e", "#e4e3de", "#fcfcfb", "#8a8984"
DARK, BLUE, LIGHT_BLUE, ORANGE, GOLD, GREEN, RED = "#2f2f2d", "#2a78d6", "#7fb0ea", "#eb6834", "#eda100", "#1baf7a", "#e34948"
plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "axes.edgecolor": GRID, "axes.labelcolor": MUTED, "axes.titlecolor": INK,
    "axes.titlesize": 12, "axes.titleweight": "normal", "axes.titlepad": 8,
    "xtick.color": MUTED, "ytick.color": MUTED, "xtick.labelsize": 10.5, "ytick.labelsize": 10,
    "font.size": 11, "axes.spines.top": False, "axes.spines.right": False, "legend.frameon": False,
})

ROOT = Path(__file__).resolve().parents[1]
DB = ROOT / "benchmarks/linux/databento"
DATA = DB / "final-demo_data"
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else DB / "final-demo_images/report"


def read(path):
    with open(path) as f:
        return list(csv.DictReader(f))


throughput = read(DATA / "throughput.csv")
latency = read(DATA / "latency.csv")


def tp(tag, scenario="none"):
    return [float(r["msgs_per_sec"]) / 1e6 for r in throughput if r["tag"] == tag and r["scenario"] == scenario]


def lat(tag, scenario="none"):
    return statistics.median(float(r["p50_ns"]) for r in latency if r["tag"] == tag and r["scenario"] == scenario)


def cpu(tag, scenario="none"):
    return statistics.median(float(r["consumer_busy_perf_pct"] or r["consumer_busy_proc_pct"])
                             for r in latency if r["tag"] == tag and r["scenario"] == scenario)


def fmt_ns(ns):
    if ns == 0:
        return "0"
    if ns >= 1e6:
        return f"{ns / 1e6:.2f} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:.1f} µs" if ns < 1e5 else f"{ns / 1e3:.0f} µs"
    return f"{ns:.0f} ns"


def style(ax, labels, title):
    ax.set_xticks(range(len(labels)), labels)
    ax.set_xlim(-0.6, len(labels) - 0.4)
    ax.set_title(title, loc="left")
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def throughput_panel(ax, items, title="throughput, M msgs/s"):
    for i, (_, runs, color) in enumerate(items):
        med = statistics.median(runs)
        ax.bar(i, med, color=color, width=0.68)
        ax.plot([i, i], [min(runs), max(runs)], color=INK, linewidth=1)
        ax.text(i, 0.4, f"{med:.1f}", ha="center", va="bottom", fontsize=11, fontweight="bold", color="white")
    ax.set_ylim(0, max(max(r) for _, r, _ in items) * 1.12)
    style(ax, [label for label, _, _ in items], title)


def latency_panel(ax, labels, values, colors, title, floor=100):
    ax.set_yscale("log")
    for i, (v, c) in enumerate(zip(values, colors)):
        if v > 0:
            ax.bar(i, v, color=c, width=0.68)
            ax.text(i, v * 1.12, fmt_ns(v), ha="center", va="bottom", fontsize=11, color=INK)
        else:
            ax.text(i, floor * 1.12, "0", ha="center", va="bottom", fontsize=11, color=INK)
    ax.set_ylim(floor, max(values) * 3)
    ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: fmt_ns(v)))
    style(ax, labels, title)


def percent_panel(ax, labels, values, colors, title):
    ax.bar(range(len(values)), values, color=colors, width=0.68)
    for i, v in enumerate(values):
        ax.text(i, v + 1.5, f"{v:.0f}%", ha="center", va="bottom", fontsize=11, color=INK)
    ax.set_ylim(0, 115)
    style(ax, labels, title)


OUT.mkdir(parents=True, exist_ok=True)

handoff = [("direct\ncall", "direct", GREEN), ("mutex\nqueue", "mutex-cv", RED),
           ("ring buffer,\nspin", "spin", ORANGE), ("ring buffer,\nyield", "yield", GOLD),
           ("ring buffer,\nback-off", "ring", BLUE)]
fig, (a, b) = plt.subplots(1, 2, figsize=(14, 4.8))
throughput_panel(a, [(label, tp(tag), color) for label, tag, color in handoff])
latency_panel(b, [l for l, _, _ in handoff], [0.0 if t == "direct" else lat(t) for _, t, _ in handoff],
              [c for _, _, c in handoff], "median latency, push to apply (log)")
fig.tight_layout(w_pad=3)
fig.savefig(OUT / "01_handoff.png", dpi=150)
plt.close(fig)

idle = [("spin", "spin", ORANGE), ("yield", "yield", GOLD), ("back-off", "ring", BLUE)]
labels, colors = [l for l, _, _ in idle], [c for _, _, c in idle]
fig, axes = plt.subplots(1, 4, figsize=(15, 4.6))
throughput_panel(axes[0], [(label, tp(tag), color) for label, tag, color in idle])
latency_panel(axes[1], labels, [lat(t) for _, t, _ in idle], colors, "median latency, core to itself (log)")
latency_panel(axes[2], labels, [lat(t, "nice0") for _, t, _ in idle], colors, "median latency, core shared (log)")
percent_panel(axes[3], labels, [cpu(t) for _, t, _ in idle], colors, "consumer CPU busy, core to itself")
fig.tight_layout(w_pad=2.5)
fig.savefig(OUT / "02_idle_strategy.png", dpi=150)
plt.close(fig)

BOOK = [("agg-map", "std::map", GRAY), ("agg-flat", "std::vector", ORANGE), ("ladder", "ladder", BLUE)]
book_labels, book_colors = [n for _, n, _ in BOOK], [c for _, _, c in BOOK]
ns = {r["book"]: float(r["median_ns"]) for r in read(DB / "reference/book_variants.csv") if r["file"] == "cme_mix"}
per_op = {r["impl"]: r for r in read(DB / "reference/book_latency.csv") if r["scenario"] == "cme_mix" and r["op"] == "all"}
fig, (a, b) = plt.subplots(1, 2, figsize=(12, 4.8), gridspec_kw={"width_ratios": [1, 1.4]})
mps = [1000 / ns[t] for t, _, _ in BOOK]
a.bar(range(3), mps, color=book_colors, width=0.62)
for i, v in enumerate(mps):
    a.text(i, v, f"{v:.1f}", ha="center", va="bottom", fontsize=11, color=INK)
a.set_ylim(0, max(mps) * 1.18)
style(a, book_labels, "throughput, M msgs/s")
width = 0.26
for k, (column, alpha) in enumerate([("p50_ns", 1.0), ("p90_ns", 0.65), ("p99_ns", 0.35)]):
    vals = [float(per_op[t][column]) for t, _, _ in BOOK]
    xs = [i + (k - 1) * width for i in range(3)]
    b.bar(xs, vals, width=width * 0.92, color=book_colors, alpha=alpha)
    for x, v in zip(xs, vals):
        b.text(x, v, f"{v:.0f}", ha="center", va="bottom", fontsize=9.5, color=INK)
b.set_ylim(0, max(float(per_op[t]["p99_ns"]) for t, _, _ in BOOK) * 1.15)
style(b, book_labels, "latency per message, ns (p50 / p90 / p99)")
fig.tight_layout(w_pad=3)
fig.savefig(OUT / "03_inner_book.png", dpi=150)
plt.close(fig)

events, ops = defaultdict(dict), {}
for r in read(DB / "perf/book_counters.csv"):
    if r["scenario"] == "cme_mix" and r["value"]:
        events[r["impl"]][(r["group"], r["event"])] = float(r["value"])
        ops[r["impl"]] = float(r["ops"])


def counter(impl, group, event):
    return events[impl][(group, event)] / ops[impl]


def bad_speculation(impl):
    e = events[impl]
    issued = e[("slots", "uops_issued.any")] / e[("slots", "cycles")]
    retired = e[("slots", "uops_retired.retire_slots")] / e[("slots", "cycles")]
    recovery = e[("frontend", "int_misc.recovery_cycles_any")] / e[("frontend", "cycles")]
    return 100 * max(0.0, (issued - retired + 4 * recovery) / 4)


panels = [
    ("cycles / msg", lambda t: counter(t, "basic", "cycles"), "{:.0f}"),
    ("instructions / msg", lambda t: counter(t, "basic", "instructions"), "{:.0f}"),
    ("IPC", lambda t: events[t][("basic", "instructions")] / events[t][("basic", "cycles")], "{:.2f}"),
    ("branch misses / msg", lambda t: counter(t, "basic", "branch-misses"), "{:.1f}"),
    ("L1 misses / msg", lambda t: counter(t, "misses", "mem_load_retired.l1_miss"), "{:.1f}"),
    ("bad speculation, % of slots", bad_speculation, "{:.0f}%"),
]
fig, axes = plt.subplots(2, 3, figsize=(13, 7.4))
for ax, (title, value, fmt) in zip(axes.flat, panels):
    vals = [value(t) for t, _, _ in BOOK]
    ax.bar(range(3), vals, color=book_colors, width=0.62)
    for i, v in enumerate(vals):
        ax.text(i, v, fmt.format(v), ha="center", va="bottom", fontsize=10.5, color=INK)
    ax.set_ylim(0, max(vals) * 1.2)
    style(ax, book_labels, title)
fig.tight_layout(h_pad=2.5, w_pad=2.5)
fig.savefig(OUT / "04_inner_book_counters.png", dpi=150)
plt.close(fig)

queue = defaultdict(dict)
items = {}
for r in read(ROOT / "benchmarks/linux/perf/queue_counters.csv"):
    if r["placement"] == "cross_core" and r["value"]:
        queue[(r["impl"], r["event"])][r["side"]] = float(r["value"])
        items[r["impl"]] = int(r["items"])

HANDOFF = [("mutex", "mutex\nqueue", RED), ("ring_spin", "ring buffer,\nspin", ORANGE),
           ("ring_yield", "ring buffer,\nyield", GOLD), ("ring", "ring buffer,\nback-off", BLUE)]
queue_panels = [
    ("cycles", "cycles / item", True, "{:.0f}"),
    ("instructions", "instructions / item", True, "{:.0f}"),
    ("mem_inst_retired.lock_loads", "locked atomic instructions / item", True, "{:.2f}"),
    ("mem_load_l3_hit_retired.xsnp_hitm", "loads hitting a line modified by the other core / item", True, "{:.2f}"),
    ("machine_clears.memory_ordering", "pipeline flushes for memory ordering / item", True, "{:.3f}"),
    ("context-switches", "context switches, whole run", False, "{:,.0f}"),
]
fig, axes = plt.subplots(2, 3, figsize=(15, 8.6))
width = 0.38
for ax, (event, title, per_item, fmt) in zip(axes.flat, queue_panels):
    top = 0
    for i, (impl, _, color) in enumerate(HANDOFF):
        for j, side in enumerate(("producer", "consumer")):
            v = queue[(impl, event)][side] / (items[impl] if per_item else 1)
            x = i + (j - 0.5) * width
            ax.bar(x, v, width=width * 0.92, color=color, alpha=0.55 if side == "producer" else 1.0)
            ax.text(x, v, fmt.format(v), ha="center", va="bottom", fontsize=8.5, color=INK)
            top = max(top, v)
    ax.set_ylim(0, top * 1.18 if top > 0 else 1)
    style(ax, [label for _, label, _ in HANDOFF], title)
fig.legend([plt.Rectangle((0, 0), 1, 1, color=GRAY, alpha=0.55), plt.Rectangle((0, 0), 1, 1, color=GRAY)],
           ["producer thread (light)", "consumer thread (dark)"], loc="upper right", ncol=2, fontsize=10.5)
fig.tight_layout(rect=(0, 0, 1, 0.95), h_pad=2.5, w_pad=2.5)
fig.savefig(OUT / "05_handoff_counters.png", dpi=150)
plt.close(fig)
