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

INK = "#0b0b0b"
MUTED = "#52514e"
GRID = "#e4e3de"
SURFACE = "#fcfcfb"
BLUE = "#2a78d6"
LIGHT_BLUE = "#7aaae6"
ORANGE = "#eb6834"
YELLOW = "#eda100"
AQUA = "#1baf7a"
DARK = "#2e2e2c"
GRAY = "#8a8984"
LIGHT_GRAY = "#c4c3be"

HANDLERS = [
    ("direct", "direct\ncall", DARK),
    ("mutex-cv", "mutex\nqueue", GRAY),
    ("locked-ring", "SPSC\nring\n(mutex)", LIGHT_GRAY),
    ("ring", "SPSC\nring\n(lock-free)", BLUE),
    ("sharded2", "sharded\nSPSC rings\n(lock-free)", LIGHT_BLUE),
]
IDLE = [("ring", "back-off", BLUE), ("spin", "spin", ORANGE), ("yield", "yield", YELLOW), ("tuned", "tuned\nback-off", AQUA)]

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "axes.edgecolor": GRID, "axes.labelcolor": MUTED, "axes.titlecolor": INK, "axes.titlesize": 14,
    "axes.titlelocation": "left", "axes.titlepad": 12, "xtick.color": MUTED, "ytick.color": MUTED,
    "xtick.labelsize": 13, "ytick.labelsize": 12, "font.size": 12, "axes.spines.top": False,
    "axes.spines.right": False, "legend.frameon": False,
})


def read(path):
    if not path.exists():
        return []
    with path.open() as f:
        return list(csv.DictReader(f))


def median(values):
    return statistics.median(values) if values else float("nan")


def nice_ns(ns):
    if ns >= 1e6:
        return f"{ns / 1e6:.1f} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:.1f} µs"
    return f"{ns:.0f} ns"


def save(fig, out, name):
    fig.savefig(out / name, dpi=200, bbox_inches="tight")
    plt.close(fig)
    print(f"wrote {out / name}")


def grid(ax, axis="y"):
    ax.grid(axis=axis, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def rates(rows, tag, scenario="none"):
    return [float(r["msgs_per_sec"]) / 1e6 for r in rows if r["tag"] == tag and r["scenario"] == scenario and r["msgs_per_sec"]]


def latency_rows(rows, tag, scenario="none"):
    return [r for r in rows if r["tag"] == tag and r["scenario"] == scenario]


def lat(rows, tag, key, scenario="none"):
    return median([float(r[key]) for r in latency_rows(rows, tag, scenario)])


def busy(rows, tag, scenario="none"):
    values = []
    for r in latency_rows(rows, tag, scenario):
        value = r["consumer_busy_perf_pct"] or r["consumer_busy_proc_pct"]
        if value:
            values.append(float(value))
    return median(values)


def neighbour(rows, tag, scenario):
    return median([float(r["neighbour_cpu_pct"]) for r in latency_rows(rows, tag, scenario) if r["neighbour_cpu_pct"]])


def throughput_bars(ax, items, values_of, light=()):
    for i, (tag, label, color) in enumerate(items):
        values = values_of(tag)
        if not values:
            continue
        ax.bar(i, median(values), width=0.7, color=color)
        ax.plot([i, i], [min(values), max(values)], color=INK, linewidth=1)
        ax.text(i, 0.25, f"{median(values):.1f}", ha="center", va="bottom", fontsize=13, fontweight="bold",
                color=INK if tag in light else "#ffffff")
    ax.set_xticks(range(len(items)), [label for _, label, _ in items])
    grid(ax)


def latency_bars(ax, items, value_of, empty_text):
    floor = 100
    for i, (tag, label, color) in enumerate(items):
        value = value_of(tag)
        if value != value:
            ax.text(i, floor * 2.2, empty_text.get(tag, "not\nmeasured"), ha="center", va="bottom", fontsize=12, color=MUTED)
            continue
        ax.bar(i, value - floor, bottom=floor, width=0.7, color=color)
        ax.text(i, value * 1.12, nice_ns(value), ha="center", va="bottom", fontsize=13, color=INK)
    ax.set_yscale("log")
    ax.set_ylim(floor, None)
    ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: nice_ns(v)))
    ax.set_xticks(range(len(items)), [label for _, label, _ in items])
    grid(ax)


def plot_handoff(throughput, latency, out):
    fig, axes = plt.subplots(1, 2, figsize=(14.5, 4.6))
    throughput_bars(axes[0], HANDLERS, lambda tag: rates(throughput, tag), light=("locked-ring",))
    axes[0].set_title("throughput, M msgs/s")
    latency_bars(axes[1], HANDLERS, lambda tag: lat(latency, tag, "p50_ns"), {"direct": "no\nhandoff"})
    axes[1].set_title("median latency, push to apply (log)")
    top = max(lat(latency, tag, "p50_ns") for tag, _, _ in HANDLERS if tag != "direct")
    axes[1].set_ylim(100, top * 3)
    fig.tight_layout()
    save(fig, out, "01_handoff_comparison.png")


def plot_idle(throughput, latency, out, scenario, name, cpu_title, cpu_value, suffix=""):
    fig, axes = plt.subplots(1, 3, figsize=(15.5, 4.6))
    throughput_bars(axes[0], IDLE, lambda tag: rates(throughput, tag, "nice0" if scenario != "none" else "none"))
    axes[0].set_title("throughput, M msgs/s" + suffix)
    latency_bars(axes[1], IDLE, lambda tag: lat(latency, tag, "p50_ns", scenario), {})
    axes[1].set_title("median latency, push to apply (log)" + suffix)
    top = max(lat(latency, tag, "p50_ns", scenario) for tag, _, _ in IDLE)
    axes[1].set_ylim(100, top * 4)
    for i, (tag, _, color) in enumerate(IDLE):
        value = cpu_value(tag)
        if value == value:
            axes[2].bar(i, value, width=0.7, color=color)
            axes[2].text(i, value + 2, f"{value:.0f}%", ha="center", va="bottom", fontsize=13, color=INK)
    axes[2].set_ylim(0, 115)
    axes[2].set_xticks(range(len(IDLE)), [label for _, label, _ in IDLE])
    axes[2].set_title(cpu_title)
    grid(axes[2])
    fig.tight_layout()
    save(fig, out, name)


def plot_sweep(sweep, out):
    timed = [r for r in sweep if r["tag"].startswith("timed-") and r["tag"].endswith("-1000") and "-0-" in r["tag"]]
    if not timed:
        return
    spins = sorted({int(r["tag"].split("-")[1]) for r in timed})
    sleeps = sorted({int(r["tag"].split("-")[3]) for r in timed})
    panels = [("none", "p50_ns", "median latency, core to itself"),
              ("nice0", "p50_ns", "median latency, busy neighbour"),
              ("none", "busy", "consumer CPU busy, core to itself")]
    fig, axes = plt.subplots(1, 3, figsize=(15.5, 5.0))
    cmap = matplotlib.colors.LinearSegmentedColormap.from_list("rank", ["#123f7a", "#2a78d6", "#9cc1ee", "#eef4fc"])
    for ax, (scenario, key, heading) in zip(axes, panels):
        matrix = []
        for spin in spins:
            line = []
            for sleep_ns in sleeps:
                tag = f"timed-{spin}-0-{sleep_ns}-1000"
                rows = [r for r in sweep if r["tag"] == tag and r["scenario"] == scenario]
                if key == "busy":
                    line.append(median([float(r["consumer_busy_perf_pct"] or r["consumer_busy_proc_pct"]) for r in rows]))
                else:
                    line.append(median([float(r[key]) for r in rows]))
            matrix.append(line)
        flat = [v for line in matrix for v in line if v == v]
        log = key != "busy"
        norm = matplotlib.colors.LogNorm(min(flat), max(flat)) if log else matplotlib.colors.Normalize(min(flat), max(flat))
        ax.imshow(matrix, cmap=cmap, norm=norm, aspect="auto")
        for i in range(len(spins)):
            for j in range(len(sleeps)):
                value = matrix[i][j]
                text = nice_ns(value) if log else f"{value:.0f}%"
                shade = (norm(value) if value == value else 0)
                ax.text(j, i, text, ha="center", va="center", fontsize=13, color="#ffffff" if shade < 0.45 else INK)
        ax.set_xticks(range(len(sleeps)), [f"sleep {s // 1000} µs" for s in sleeps])
        ax.set_yticks(range(len(spins)), [f"spin {s // 1000} µs" for s in spins])
        ax.tick_params(length=0)
        for spine in ax.spines.values():
            spine.set_visible(False)
        ax.set_title(heading)
    fig.tight_layout()
    save(fig, out, "04_backoff_tuning.png")


def sweep_table(sweep, out):
    groups = defaultdict(lambda: defaultdict(list))
    for r in sweep:
        groups[r["tag"]][r["scenario"]].append(r)
    lines = ["| config | p50 alone | p99 alone | CPU alone | p50 nice 0 | p99 nice 0 | neighbour CPU nice 0 | p50 nice 19 | neighbour CPU nice 19 |",
             "|---|---|---|---|---|---|---|---|---|"]
    for tag, scenarios in groups.items():
        def pick(scenario, key):
            rows = scenarios.get(scenario, [])
            values = [float(r[key]) for r in rows if r[key]]
            return median(values)
        alone_busy = median([float(r["consumer_busy_perf_pct"] or r["consumer_busy_proc_pct"]) for r in scenarios.get("none", [])])
        lines.append(f"| {tag} | {nice_ns(pick('none', 'p50_ns'))} | {nice_ns(pick('none', 'p99_ns'))} | {alone_busy:.0f}% | "
                     f"{nice_ns(pick('nice0', 'p50_ns'))} | {nice_ns(pick('nice0', 'p99_ns'))} | {pick('nice0', 'neighbour_cpu_pct'):.0f}% | "
                     f"{nice_ns(pick('nice19', 'p50_ns'))} | {pick('nice19', 'neighbour_cpu_pct'):.0f}% |")
    (out / "idle_sweep.md").write_text("\n".join(lines) + "\n")
    print(f"wrote {out / 'idle_sweep.md'}")


OVERSUB_PLACEMENTS = [("fits", "3 shards\n4 cores"), ("tight", "6 shards\n4 cores"), ("oversub", "6 shards\n2 cores")]
OVERSUB_STRATEGIES = [("spin", ORANGE), ("yield", YELLOW), ("back-off", BLUE), ("tuned", AQUA), ("mutex-cv", GRAY)]
OVERSUB_NAMES = {"spin": "spin", "yield": "yield", "back-off": "back-off", "tuned": "tuned back-off", "mutex-cv": "mutex + condvar"}


def plot_oversubscribed(throughput, latency, out):
    fig, axes = plt.subplots(1, 3, figsize=(17, 5.2))
    width = 0.8 / len(OVERSUB_STRATEGIES)
    panels = [(axes[0], "throughput, M msgs/s"), (axes[1], "median latency, push to apply (log)"),
              (axes[2], "cores busy (all threads)")]
    for s_i, (strategy, color) in enumerate(OVERSUB_STRATEGIES):
        for p_i, (placement, _) in enumerate(OVERSUB_PLACEMENTS):
            x = p_i + (s_i - (len(OVERSUB_STRATEGIES) - 1) / 2) * width
            values = [float(r["msgs_per_sec"]) / 1e6 for r in throughput
                      if r["placement"] == placement and r["strategy"] == strategy and r["msgs_per_sec"]]
            if values:
                axes[0].bar(x, median(values), width=width * 0.92, color=color,
                            label=OVERSUB_NAMES[strategy] if p_i == 0 else None)
                axes[0].plot([x, x], [min(values), max(values)], color=INK, linewidth=1)
                axes[0].text(x, max(values) + 0.2, f"{median(values):.1f}", ha="center", va="bottom", fontsize=9, color=INK)
            rows = [r for r in latency if r["placement"] == placement and r["strategy"] == strategy]
            if rows:
                value = median([float(r["p50_ns"]) for r in rows])
                axes[1].bar(x, value - 100, bottom=100, width=width * 0.92, color=color)
                axes[1].text(x, value * 1.15, nice_ns(value), ha="center", va="bottom", fontsize=8, color=INK, rotation=90)
                busy_values = [float(r["cores_busy_pct"]) for r in rows if r["cores_busy_pct"]]
                if busy_values:
                    axes[2].bar(x, median(busy_values), width=width * 0.92, color=color)
                    axes[2].text(x, median(busy_values) + 1.5, f"{median(busy_values):.0f}%", ha="center", va="bottom",
                                 fontsize=8, color=INK, rotation=90)
    for ax, heading in panels:
        ax.set_xticks(range(len(OVERSUB_PLACEMENTS)), [label for _, label in OVERSUB_PLACEMENTS])
        ax.set_title(heading)
        grid(ax)
    axes[1].set_yscale("log")
    top = max((float(r["p50_ns"]) for r in latency), default=1000)
    axes[1].set_ylim(100, top * 8)
    axes[1].yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: nice_ns(v)))
    axes[2].set_ylim(0, 118)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower left", bbox_to_anchor=(0.01, -0.06), ncol=len(labels), fontsize=12)
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    save(fig, out, "05_oversubscribed_shards.png")


def main():
    data = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("benchmarks/linux/databento/final-demo_data")
    images = Path(sys.argv[2]) if len(sys.argv) > 2 else data.parent / "final-demo_images"
    images.mkdir(parents=True, exist_ok=True)
    throughput = read(data / "throughput.csv")
    latency = read(data / "latency.csv")
    sweep = read(data / "idle_sweep.csv")
    if throughput and latency:
        plot_handoff(throughput, latency, images)
        plot_idle(throughput, latency, images, "none", "02_idle_strategies.png", "consumer CPU busy",
                  lambda tag: busy(latency, tag))
        plot_idle(throughput, latency, images, "nice0", "03_idle_strategies_shared_core.png",
                  "CPU left for the neighbour", lambda tag: neighbour(latency, tag, "nice0"), ", core shared")
    oversub_throughput = read(data / "oversubscribed_throughput.csv")
    oversub_latency = read(data / "oversubscribed_latency.csv")
    if oversub_throughput or oversub_latency:
        plot_oversubscribed(oversub_throughput, oversub_latency, images)
    if sweep:
        plot_sweep(sweep, images)
        sweep_table(sweep, data)


if __name__ == "__main__":
    main()
