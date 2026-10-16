#!/usr/bin/env python3
import csv
import re
import shutil
import statistics
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker

INK = "#0b0b0b"
MUTED = "#52514e"
GRID = "#e4e3de"
SURFACE = "#fcfcfb"
GRAY = "#8a8984"
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948"]
SIZE = (12, 6.75)

MODE_ORDER = ["single", "mutex/1", "mutex/64", "ring/1", "ring/64", "sharded2", "sharded3"]
MODE_NAMES = {"single": "single thread", "mutex/1": "mutex + condvar,\nbatch 1", "mutex/64": "mutex + condvar,\nbatch 64",
              "ring/1": "ring, batch 1", "ring/64": "ring, batch 64",
              "sharded2": "2 shards", "sharded3": "3 shards"}
HANDOFF_ORDER = ["mutex-cv", "locked-ring", "ring-yield", "ring-spin", "ring", "sequenced"]
HANDOFF_NAMES = {"mutex-cv": "std::queue + mutex + condvar", "locked-ring": "ring under a mutex, polled",
                 "ring-yield": "SPSC ring, yield", "ring-spin": "SPSC ring, spin",
                 "ring": "SPSC ring, back-off", "sequenced": "per-slot sequence ring"}
HANDOFF_TICKS = {"mutex-cv": "mutex +\ncondvar", "locked-ring": "locked\nring", "ring-yield": "ring,\nyield",
                 "ring-spin": "ring,\nspin", "ring": "ring,\nback-off", "sequenced": "sequence\nring"}
HANDOFF_COLORS = dict(zip(HANDOFF_ORDER, [GRAY, SERIES[4], SERIES[3], SERIES[1], SERIES[0], SERIES[2]]))
PLACEMENT_ORDER = ["cross_core", "smt", "one_cpu"]
PLACEMENT_NAMES = {"cross_core": "separate physical cores", "smt": "hyperthreads of one core",
                   "one_cpu": "one logical CPU (time-shared)"}
PLACEMENT_COLORS = dict(zip(PLACEMENT_ORDER, SERIES[:3]))
ORDERS_NAMES = {"aggregate": "market maker: totals per level", "list": "exchange: std::list queue",
                "pooled-fifo": "exchange: pooled FIFO queue", "lazy-fifo": "exchange: lazy FIFO queue"}
ORDERS_COLORS = {"aggregate": SERIES[0], "list": GRAY, "pooled-fifo": SERIES[1], "lazy-fifo": SERIES[2]}
LEVEL_NAMES = {"map": "std::map", "vector-linear": "sorted vector, linear", "vector-hybrid": "sorted vector, 8 probes + binary",
               "ladder": "price ladder (one slot per tick)"}

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "axes.edgecolor": GRID, "axes.labelcolor": MUTED, "axes.titlecolor": INK,
    "axes.titlesize": 13, "axes.titleweight": "bold", "axes.titlepad": 10, "axes.labelsize": 12,
    "xtick.color": MUTED, "ytick.color": MUTED, "xtick.labelsize": 11, "ytick.labelsize": 11,
    "font.size": 11, "axes.spines.top": False, "axes.spines.right": False, "legend.frameon": False,
    "legend.fontsize": 11,
})


def read(path):
    if not path.exists():
        return []
    with path.open() as f:
        return list(csv.DictReader(f))


def median(values):
    return statistics.median(values) if values else float("nan")


def title(fig, text, subtitle=None):
    fig.suptitle(text, x=0.012, y=0.985, ha="left", va="top", fontsize=17, fontweight="bold", color=INK)
    if subtitle:
        fig.text(0.012, 0.935, subtitle, ha="left", va="top", fontsize=12, color=MUTED)


def save(fig, images, name):
    fig.savefig(images / name, dpi=200, bbox_inches="tight")
    plt.close(fig)
    print(f"wrote {images / name}")


def bottom_legend(fig, ax, heading=None):
    handles, labels = ax.get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower left", bbox_to_anchor=(0.012, -0.02), ncol=len(labels), title=heading,
               title_fontsize=11, alignment="left")


def value_grid(ax, axis="y"):
    ax.grid(axis=axis, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def label_bar(ax, x, y, text, vertical=True):
    if vertical:
        ax.text(x, y, text, ha="center", va="bottom", fontsize=10, color=INK)
    else:
        ax.text(y, x, f" {text}", ha="left", va="center", fontsize=10, color=INK)


def files_of(rows):
    return list(dict.fromkeys(row["file"] for row in rows))


def books_of(rows):
    return list(dict.fromkeys(row["book"] for row in rows if row["mode"] != "parse"))


def rates(rows, **match):
    return [float(r["msgs_per_sec"]) / 1e6 for r in rows if r["msgs_per_sec"] and all(r[k] == v for k, v in match.items())]


def mode_key(row):
    return f"{row['mode']}/{row['batch']}" if row["mode"] in ("ring", "mutex") else row["mode"]


def plot_modes(rows, images):
    rows = [r for r in rows if r["experiment"] == "handoff"]
    for file in files_of(rows):
        books = books_of([r for r in rows if r["file"] == file])
        fig, axes = plt.subplots(1, 2, figsize=SIZE, sharey=True)
        for ax, compression in zip(axes, ("zstd", "none")):
            subset = [r for r in rows if r["file"] == file and r["compression"] == compression]
            ceiling = median(rates(subset, mode="parse"))
            width = 0.8 / max(len(books), 1)
            for b, book in enumerate(books):
                for m, mode in enumerate(MODE_ORDER):
                    values = [float(r["msgs_per_sec"]) / 1e6 for r in subset
                              if r["book"] == book and mode_key(r) == mode and r["msgs_per_sec"]]
                    if not values:
                        continue
                    x = m + (b - (len(books) - 1) / 2) * width
                    ax.bar(x, median(values), width=width * 0.92, color=SERIES[b], label=book if m == 0 else None)
                    ax.plot([x, x], [min(values), max(values)], color=INK, linewidth=1)
                    label_bar(ax, x, max(values), f"{median(values):.1f}")
            if ceiling == ceiling:
                ax.axhline(ceiling, color=INK, linestyle="--", linewidth=1.2)
                ax.text(-0.45, ceiling, f"producer alone: {ceiling:.1f}", ha="left", va="bottom", fontsize=10,
                        color=INK, bbox={"facecolor": SURFACE, "edgecolor": "none", "pad": 1.5})
            ax.set_xticks(range(len(MODE_ORDER)), [MODE_NAMES[m] for m in MODE_ORDER], rotation=30, ha="right", fontsize=10)
            ax.set_title("zstd-compressed file" if compression == "zstd" else "decompressed file (like a live feed)",
                         loc="left")
            value_grid(ax)
        axes[0].set_ylabel("million messages per second")
        bottom_legend(fig, axes[0], "book")
        title(fig, f"Databento replay throughput by handoff design ({file})",
              "Median of runs, line = min to max. Dashed line: parse and decode only, no book (the producer's ceiling).")
        fig.tight_layout(rect=(0, 0.04, 1, 0.92))
        save(fig, images, f"01_pipeline_modes_{file}.png")


def plot_buffers(rows, images):
    rows = [r for r in rows if r["experiment"] == "buffers"]
    for file in files_of(rows):
        books = books_of([r for r in rows if r["file"] == file])
        fig, axes = plt.subplots(1, 2, figsize=SIZE, sharey=True)
        for ax, batch in zip(axes, ("1", "64")):
            width = 0.8 / max(len(books), 1)
            for b, book in enumerate(books):
                for h, handoff in enumerate(HANDOFF_ORDER):
                    values = rates(rows, file=file, book=book, batch=batch, handoff=handoff)
                    if not values:
                        continue
                    x = h + (b - (len(books) - 1) / 2) * width
                    ax.bar(x, median(values), width=width * 0.92, color=SERIES[b], label=book if h == 0 else None)
                    ax.plot([x, x], [min(values), max(values)], color=INK, linewidth=1)
                    label_bar(ax, x, max(values), f"{median(values):.1f}")
            ax.set_xticks(range(len(HANDOFF_ORDER)), [HANDOFF_TICKS[h] for h in HANDOFF_ORDER])
            ax.set_title(f"consumer drains up to {batch} message{'s' if batch != '1' else ''} per wake", loc="left")
            value_grid(ax)
        axes[0].set_ylabel("million messages per second")
        bottom_legend(fig, axes[0], "book")
        title(fig, f"Same pipeline, different queue and idle strategy ({file}, decompressed)",
              "Separate physical cores, median of runs. locked ring = ring under a mutex, polled; "
              "ring = lock-free SPSC; sequence ring = per-slot sequence numbers.")
        fig.tight_layout(rect=(0, 0.04, 1, 0.92))
        save(fig, images, f"02_queue_types_{file}.png")


def plot_placement(rows, images):
    rows = [r for r in rows if r["experiment"] == "placement"]
    for file in files_of(rows):
        books = books_of([r for r in rows if r["file"] == file])
        groups = [(book, mode) for book in books for mode in ("single", "ring/64")]
        placements = [p for p in PLACEMENT_ORDER if any(r["placement"] == p for r in rows)]
        fig, ax = plt.subplots(figsize=SIZE)
        width = 0.8 / len(placements)
        for p, placement in enumerate(placements):
            for g, (book, mode) in enumerate(groups):
                values = [float(r["msgs_per_sec"]) / 1e6 for r in rows if r["file"] == file and r["book"] == book
                          and mode_key(r) == mode and r["placement"] == placement and r["msgs_per_sec"]]
                if not values:
                    continue
                x = g + (p - (len(placements) - 1) / 2) * width
                ax.bar(x, median(values), width=width * 0.92, color=PLACEMENT_COLORS[placement],
                       label=PLACEMENT_NAMES[placement] if g == 0 else None)
                ax.plot([x, x], [min(values), max(values)], color=INK, linewidth=1)
                label_bar(ax, x, max(values), f"{median(values):.1f}")
        ax.set_xticks(range(len(groups)), [f"{book}\n{MODE_NAMES[mode]}" for book, mode in groups])
        ax.set_ylabel("million messages per second")
        value_grid(ax)
        bottom_legend(fig, ax)
        title(fig, f"Where the threads run ({file}, decompressed)",
              "Single thread is pinned the same way, as a reference. Median of runs, line = min to max.")
        fig.tight_layout(rect=(0, 0.04, 1, 0.92))
        save(fig, images, f"03_thread_placement_{file}.png")


def plot_latency(rows, images):
    if not rows:
        return
    file = rows[0]["file"]
    speed = rows[0]["speed"]
    books = list(dict.fromkeys(r["book"] for r in rows))
    batches = sorted({r["batch"] for r in rows}, key=int)
    fig, axes = plt.subplots(len(books), len(batches), figsize=SIZE, sharey=True, squeeze=False)
    marks = [("p50_ns", "median", SERIES[0], "o"), ("p99_ns", "p99", SERIES[1], "s"),
             ("p999_ns", "p99.9", SERIES[2], "D"), ("p9999_ns", "p99.99", SERIES[4], "^")]
    for r_i, book in enumerate(books):
        for c_i, batch in enumerate(batches):
            ax = axes[r_i][c_i]
            for h, handoff in enumerate(HANDOFF_ORDER):
                row = next((r for r in rows if r["book"] == book and r["batch"] == batch and r["handoff"] == handoff), None)
                if row is None:
                    continue
                values = [float(row[key]) for key, *_ in marks]
                ax.plot([h, h], [values[0], values[-1]], color=GRID, linewidth=3, zorder=1)
                for (key, label, color, marker), value in zip(marks, values):
                    ax.scatter(h, value, color=color, marker=marker, s=60, zorder=2, edgecolors=SURFACE, linewidths=1.2,
                               label=label if h == 0 else None)
            ax.set_yscale("log")
            ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: nice_ns(v)))
            ax.set_xticks(range(len(HANDOFF_ORDER)), [HANDOFF_TICKS[h] for h in HANDOFF_ORDER] if r_i == len(books) - 1 else [],
                          fontsize=10)
            ax.set_title(f"{book}, batch {batch}", loc="left", fontsize=12)
            value_grid(ax)
    bottom_legend(fig, axes[0][0])
    title(fig, f"Time from push to the consumer picking the message up ({file})",
          f"Paced at {speed}x market time, separate cores. Median = the handoff itself; the tail = waiting behind a burst, "
          "plus waking a sleeping consumer.")
    fig.tight_layout(rect=(0, 0.04, 1, 0.92))
    save(fig, images, "04_handoff_latency.png")


def nice_ns(ns):
    if ns >= 1e6:
        return f"{ns / 1e6:g} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:g} µs"
    return f"{ns:g} ns"


def plot_latency_cdf(rows, images):
    if not rows:
        return
    file = rows[0]["file"]
    books = list(dict.fromkeys(r["book"] for r in rows))
    batch = "64" if any(r["batch"] == "64" for r in rows) else rows[0]["batch"]
    fig, axes = plt.subplots(1, len(books), figsize=SIZE, sharey=True, squeeze=False)
    for ax, book in zip(axes[0], books):
        for handoff in HANDOFF_ORDER:
            points = sorted((float(r["p"]), float(r["latency_ns"])) for r in rows
                            if r["book"] == book and r["batch"] == batch and r["handoff"] == handoff)
            if not points:
                continue
            ax.plot([max(l, 1.0) for _, l in points], [max(1 - p, 1e-4) for p, _ in points],
                    color=HANDOFF_COLORS[handoff], linewidth=2.2, label=HANDOFF_NAMES[handoff])
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: nice_ns(v)))
        ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{100 * v:g}%"))
        ax.set_xlabel("push-to-pickup latency")
        ax.set_title(f"{book}, batch {batch}", loc="left")
        ax.grid(color=GRID, linewidth=0.8)
    axes[0][0].set_ylabel("share of messages slower than this")
    axes[0][-1].legend(loc="lower left")
    title(fig, f"Latency tail by queue type ({file})",
          "Further left and lower is better. Consumers that sleep pay the CPU's wake-up time (up to ~0.9 ms on this laptop).")
    fig.tight_layout(rect=(0, 0.04, 1, 0.92))
    save(fig, images, "05_handoff_latency_tail.png")


def plot_consumer_cpu(rows, images):
    if not rows:
        return
    books = list(dict.fromkeys(r["book"] for r in rows))
    batches = sorted({r["batch"] for r in rows}, key=int)
    fig, ax = plt.subplots(figsize=SIZE)
    groups = [(book, batch) for book in books for batch in batches]
    width = 0.8 / len(HANDOFF_ORDER)
    for h, handoff in enumerate(HANDOFF_ORDER):
        for g, (book, batch) in enumerate(groups):
            row = next((r for r in rows if r["book"] == book and r["batch"] == batch and r["handoff"] == handoff), None)
            if row is None:
                continue
            x = g + (h - (len(HANDOFF_ORDER) - 1) / 2) * width
            value = float(row["consumer_busy_pct"])
            ax.bar(x, value, width=width * 0.92, color=HANDOFF_COLORS[handoff], label=HANDOFF_NAMES[handoff] if g == 0 else None)
            label_bar(ax, x, value, f"{value:.0f}%")
    ax.set_xticks(range(len(groups)), [f"{book}, batch {batch}" for book, batch in groups])
    ax.set_ylim(0, 115)
    ax.set_ylabel("consumer core busy, % of wall time")
    value_grid(ax)
    bottom_legend(fig, ax)
    title(fig, f"What waiting costs: consumer CPU during the paced replay ({rows[0]['file']})",
          "Busy share of the consumer's core from the kernel's idle counters. Spinning keeps the core busy even when there is nothing to do.")
    fig.tight_layout(rect=(0, 0.04, 1, 0.92))
    save(fig, images, "06_consumer_cpu.png")


def plot_books(rows, images):
    if not rows:
        return
    skip = {"tree-nocache", "ladder-nocache", "ladder-identity"}
    files = files_of(rows)
    fig, axes = plt.subplots(1, len(files), figsize=SIZE, squeeze=False)
    for ax, file in zip(axes[0], files):
        subset = [r for r in rows if r["file"] == file and r["book"] not in skip]
        subset.sort(key=lambda r: -float(r["median_ns"]))
        for i, row in enumerate(subset):
            value = float(row["median_ns"])
            ax.barh(i, value, height=0.7, color=ORDERS_COLORS.get(row["orders"], GRAY))
            label_bar(ax, i, value, f"{value:.1f}", vertical=False)
        ax.set_yticks(range(len(subset)), [r["book"] for r in subset])
        ax.set_xlabel("ns per record (median of 15 passes)")
        ax.set_title(file, loc="left")
        ax.margins(x=0.15)
        value_grid(ax, "x")
    kinds = [k for k in ORDERS_NAMES if any(r["orders"] == k for r in rows)]
    fig.legend([plt.Rectangle((0, 0), 1, 1, color=ORDERS_COLORS[k]) for k in kinds], [ORDERS_NAMES[k] for k in kinds],
               loc="lower left", bbox_to_anchor=(0.01, -0.06), ncol=2)
    title(fig, "Inner book designs on real CME records (book only, no I/O)",
          "Every book ends in the same state. Faster is shorter.")
    fig.tight_layout(rect=(0, 0.04, 1, 0.92))
    save(fig, images, "07_inner_books.png")


def plot_book_matrix(rows, images):
    if not rows:
        return
    file = max(files_of(rows), key=lambda f: sum(1 for r in rows if r["file"] == f and float(r["resting_orders"]) > 0))
    pick = {}
    for row in rows:
        if row["file"] != file or row["cache"] != "64" or row["book"] == "ladder-identity":
            continue
        key = (row["levels"], row["orders"])
        current = pick.get(key)
        if current is None or float(row["median_ns"]) < float(current["median_ns"]):
            pick[key] = row
    levels = [l for l in LEVEL_NAMES if any(k[0] == l for k in pick)]
    orders = [o for o in ("aggregate", "list", "pooled-fifo", "lazy-fifo") if any(k[1] == o for k in pick)]
    values = [[float(pick[(l, o)]["median_ns"]) if (l, o) in pick else float("nan") for o in orders] for l in levels]
    fig, ax = plt.subplots(figsize=SIZE)
    cmap = matplotlib.colors.LinearSegmentedColormap.from_list("ns", ["#123f7a", "#2a78d6", "#9cc1ee", "#eef4fc"])
    finite = [v for line in values for v in line if v == v]
    ax.imshow(values, cmap=cmap, vmin=min(finite), vmax=max(finite), aspect="auto")
    for i, level in enumerate(levels):
        for j, kind in enumerate(orders):
            row = pick.get((level, kind))
            if row is None:
                ax.text(j, i, "not built", ha="center", va="center", color=MUTED, fontsize=11)
                continue
            value = float(row["median_ns"])
            dark = value < min(finite) + 0.45 * (max(finite) - min(finite))
            ax.text(j, i, f"{value:.1f} ns\n{row['book']}", ha="center", va="center", fontsize=12,
                    color="#ffffff" if dark else INK)
    ax.set_xticks(range(len(orders)), [ORDERS_NAMES[o].replace(": ", ":\n") for o in orders])
    ax.set_yticks(range(len(levels)), [LEVEL_NAMES[l] for l in levels])
    ax.tick_params(length=0)
    for spine in ax.spines.values():
        spine.set_visible(False)
    title(fig, f"Price-level container x what each level stores ({file})",
          "ns per record, book only, fastest variant per cell. Darker is faster.")
    fig.tight_layout(rect=(0, 0.04, 1, 0.92))
    save(fig, images, "08_book_design_matrix.png")


def plot_book_op_latency(rows, images):
    if not rows:
        return
    file = rows[0]["scenario"]
    books = [b for b in ["tree", "hybrid", "agg-flat", "ladder", "pooled-ladder", "lazy-handle-ladder"]
             if any(r["impl"] == b for r in rows)]
    ops = [op for op in ["add", "cancel", "modify", "all"] if any(r["op"] == op and r["scenario"] == file for r in rows)]
    fig, axes = plt.subplots(1, len(ops), figsize=SIZE, sharey=True, squeeze=False)
    marks = [("p50_ns", "median", SERIES[0], "o"), ("p99_ns", "p99", SERIES[1], "s"), ("p999_ns", "p99.9", SERIES[2], "D")]
    for ax, op in zip(axes[0], ops):
        for i, book in enumerate(books):
            row = next((r for r in rows if r["scenario"] == file and r["impl"] == book and r["op"] == op), None)
            if row is None:
                continue
            values = [float(row[key]) for key, *_ in marks]
            ax.plot([values[0], values[-1]], [i, i], color=GRID, linewidth=3, zorder=1)
            for (key, label, color, marker), value in zip(marks, values):
                ax.scatter(value, i, color=color, marker=marker, s=60, zorder=2, edgecolors=SURFACE, linewidths=1.2,
                           label=label if i == 0 else None)
        ax.set_xscale("log")
        ax.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: nice_ns(v)))
        ax.xaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
        ax.set_title(op, loc="left")
        value_grid(ax, "x")
    axes[0][0].set_yticks(range(len(books)), books)
    axes[0][0].invert_yaxis()
    bottom_legend(fig, axes[0][-1])
    title(fig, f"Per-message latency inside the book ({file})",
          "Cycle counter around each real MBO record (includes the instrument lookup and a best bid/ask read).")
    fig.tight_layout(rect=(0, 0.04, 1, 0.92))
    save(fig, images, "09_book_message_latency.png")


def perf_charts(out, images):
    perf = out / "perf"
    if not (perf / "book_counters.csv").exists():
        return
    subprocess.run([sys.executable, str(Path(__file__).with_name("plot_linux_perf.py")), str(perf)], check=True)
    for index, (source, name) in enumerate([("book_work.png", "work_per_record"), ("book_topdown.png", "topdown"),
                                            ("book_misses.png", "cache_misses"), ("book_mem_sources.png", "slow_loads_by_structure"),
                                            ("book_hotspots.png", "hot_functions")], start=10):
        if (perf / source).exists():
            shutil.copy(perf / source, images / f"{index}_{name}.png")
            print(f"wrote {images / f'{index}_{name}.png'}")


COMBO_BOOKS = ["tree-cached64", "hybrid-cached64", "agg-flat-cached64", "ladder-cached64", "pooled-hybrid",
               "pooled-ladder", "lazy-handle-ladder"]
COMBO_BOOK_NAMES = {"tree-cached64": "tree", "hybrid-cached64": "hybrid", "agg-flat-cached64": "agg-flat",
                    "ladder-cached64": "ladder", "pooled-hybrid": "pooled-hybrid", "pooled-ladder": "pooled-ladder",
                    "lazy-handle-ladder": "lazy-handle-ladder"}
COMBO_HANDLERS = ["single", "mutex-cv", "locked-ring", "ring-yield", "ring-spin", "ring", "sequenced", "sharded2",
                  "sharded3"]
COMBO_HANDLER_NAMES = dict(HANDOFF_TICKS, single="single\nthread", sharded2="2 shards", sharded3="3 shards")


def combo_heatmap(ax, matrix, rows, cols, fmt, reverse):
    finite = [v for line in matrix for v in line if v == v]
    colors = ["#123f7a", "#2a78d6", "#9cc1ee", "#eef4fc"]
    cmap = matplotlib.colors.LinearSegmentedColormap.from_list("rank", colors[::-1] if not reverse else colors)
    ax.imshow(matrix, cmap=cmap, vmin=min(finite), vmax=max(finite), aspect="auto")
    span = max(finite) - min(finite) or 1
    for i in range(len(rows)):
        for j in range(len(cols)):
            value = matrix[i][j]
            if value != value:
                ax.text(j, i, "-", ha="center", va="center", color=MUTED)
                continue
            strength = (value - min(finite)) / span
            dark = strength < 0.45 if reverse else strength > 0.55
            ax.text(j, i, fmt(value), ha="center", va="center", fontsize=10, color="#ffffff" if dark else INK)
    ax.set_xticks(range(len(cols)), [COMBO_HANDLER_NAMES.get(c, c) for c in cols], fontsize=10)
    ax.set_yticks(range(len(rows)), [COMBO_BOOK_NAMES.get(r, r) for r in rows])
    ax.tick_params(length=0)
    for spine in ax.spines.values():
        spine.set_visible(False)


def combo_throughput(rows):
    cells = defaultdict(list)
    for row in rows:
        if row["msgs_per_sec"]:
            cells[(row["book"], row["handler"])].append(float(row["msgs_per_sec"]) / 1e6)
    return {key: median(values) for key, values in cells.items()}


def plot_combos(out, images):
    throughput = combo_throughput(read(out / "combos_throughput.csv"))
    latency = {(r["book"], r["handoff"]): r for r in read(out / "combos_latency.csv")}
    lines = []
    if throughput:
        books = [b for b in COMBO_BOOKS if any(k[0] == b for k in throughput)]
        handlers = [h for h in COMBO_HANDLERS if any(k[1] == h for k in throughput)]
        matrix = [[throughput.get((b, h), float("nan")) for h in handlers] for b in books]
        fig, ax = plt.subplots(figsize=SIZE)
        combo_heatmap(ax, matrix, books, handlers, lambda v: f"{v:.1f}", reverse=False)
        title(fig, "Throughput of every handler x inner book (cme_mix, decompressed)",
              "Million messages per second, median of runs. Darker is faster. Every book uses a 64-slot instrument cache.")
        fig.tight_layout(rect=(0, 0, 1, 0.92))
        save(fig, images, "15_combo_throughput.png")
        ranked = sorted(throughput.items(), key=lambda kv: -kv[1])
        lines += ["## Throughput, best first (M msgs/s)", "", "| rank | handler | book | M msgs/s |", "|---|---|---|---|"]
        lines += [f"| {i} | {h} | {COMBO_BOOK_NAMES.get(b, b)} | {v:.2f} |" for i, ((b, h), v) in enumerate(ranked, 1)]
        lines.append("")
    if latency:
        books = [b for b in COMBO_BOOKS if any(k[0] == b for k in latency)]
        handlers = [h for h in HANDOFF_ORDER if any(k[1] == h for k in latency)]
        fig, axes = plt.subplots(1, 2, figsize=SIZE, sharey=True)
        for ax, key, label in zip(axes, ("p50_ns", "p99_ns"), ("median", "p99")):
            matrix = [[float(latency[(b, h)][key]) if (b, h) in latency else float("nan") for h in handlers] for b in books]
            combo_heatmap(ax, matrix, books, handlers, nice_ns_short, reverse=True)
            ax.set_title(label, loc="left")
        title(fig, f"Push-to-pickup latency of every queue x inner book ({next(iter(latency.values()))['file']})",
              f"Paced at {next(iter(latency.values()))['speed']}x market time, batch 64, separate cores. Darker is faster.")
        fig.tight_layout(rect=(0, 0, 1, 0.92))
        save(fig, images, "16_combo_latency.png")
        ranked = sorted(latency.items(), key=lambda kv: float(kv[1]["p50_ns"]))
        lines += ["## Latency, best median first", "", "| rank | queue | book | median | p90 | p99 | p99.9 |",
                  "|---|---|---|---|---|---|---|"]
        lines += [f"| {i} | {h} | {COMBO_BOOK_NAMES.get(b, b)} | {nice_ns_short(float(r['p50_ns']))} | "
                  f"{nice_ns_short(float(r['p90_ns']))} | {nice_ns_short(float(r['p99_ns']))} | {nice_ns_short(float(r['p999_ns']))} |"
                  for i, ((b, h), r) in enumerate(ranked, 1)]
        lines.append("")
    if throughput and latency:
        points = [(b, h, throughput[(b, h)], float(latency[(b, h)]["p50_ns"])) for (b, h) in latency if (b, h) in throughput]
        fig, ax = plt.subplots(figsize=SIZE)
        for handler in HANDOFF_ORDER:
            subset = [p for p in points if p[1] == handler]
            ax.scatter([p[3] for p in subset], [p[2] for p in subset], s=90, color=HANDOFF_COLORS[handler],
                       edgecolors=SURFACE, linewidths=1.5, label=HANDOFF_NAMES[handler], zorder=3)
        frontier = []
        for point in sorted(points, key=lambda p: p[3]):
            if not frontier or point[2] > frontier[-1][2]:
                frontier.append(point)
        ax.plot([p[3] for p in frontier], [p[2] for p in frontier], color=GRAY, linestyle="--", linewidth=1.2, zorder=2)
        for k, (book, handler, rate, lat) in enumerate(sorted(frontier, key=lambda p: -p[2])):
            ax.annotate(f"{COMBO_BOOK_NAMES.get(book, book)} + {HANDOFF_NAMES[handler]} ({rate:.1f} M/s)", (lat, rate),
                        xycoords="data", xytext=(0.6, 0.95 - 0.07 * k), textcoords="axes fraction", va="top", fontsize=10,
                        color=INK, arrowprops={"arrowstyle": "-", "color": GRAY, "linewidth": 0.8})
        ax.set_xscale("log")
        ax.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: nice_ns(v)))
        ax.set_xlabel("median push-to-pickup latency (lower is better)")
        ax.set_ylabel("million messages per second (higher is better)")
        ax.grid(color=GRID, linewidth=0.8)
        bottom_legend(fig, ax)
        title(fig, "Throughput vs latency for every queue x inner book",
              "Each point is one combination. Dashed line: the combinations nothing else beats on both axes (labeled).")
        fig.tight_layout(rect=(0, 0.04, 1, 0.92))
        save(fig, images, "17_combo_tradeoff.png")
        lines += ["## Best trade-offs (nothing beats them on both throughput and median latency)", "",
                  "| handler | book | M msgs/s | median |", "|---|---|---|---|"]
        lines += [f"| {h} | {COMBO_BOOK_NAMES.get(b, b)} | {r:.2f} | {nice_ns_short(l)} |" for b, h, r, l in frontier]
    if lines:
        (out / "combos_ranking.md").write_text("# Handler x inner book rankings\n\n" + "\n".join(lines) + "\n")
        print(f"wrote {out / 'combos_ranking.md'}")


def nice_ns_short(ns):
    if ns >= 1e6:
        return f"{ns / 1e6:.2f} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:.1f} µs"
    return f"{ns:.0f} ns"


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: plot_databento.py <benchmarks/linux/databento>")
    out = Path(sys.argv[1])
    images = out / "images"
    images.mkdir(parents=True, exist_ok=True)
    throughput = read(out / "throughput.csv")
    plot_modes(throughput, images)
    plot_buffers(throughput, images)
    plot_placement(throughput, images)
    plot_latency(read(out / "latency.csv"), images)
    plot_latency_cdf(read(out / "latency.csv.cdf"), images)
    plot_consumer_cpu(read(out / "consumer_cpu.csv"), images)
    variants = read(out / "book_variants.csv")
    plot_books(variants, images)
    plot_book_matrix(variants, images)
    plot_book_op_latency(read(out / "book_latency.csv"), images)
    perf_charts(out, images)
    plot_combos(out, images)


if __name__ == "__main__":
    main()
