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
from matplotlib.colors import LinearSegmentedColormap

INK = "#0b0b0b"
MUTED = "#52514e"
GRID = "#e4e3de"
SURFACE = "#fcfcfb"
BASELINE = "#8a8984"
SERIES = ["#2a78d6", "#eb6834", "#1baf7a"]
ORDER_COLORS = {"list": BASELINE, "aggregate": SERIES[0], "pooled-fifo": SERIES[1]}
ORDER_NAMES = {"list": "std::list per level (full queue)", "aggregate": "aggregate levels, orders in the index",
               "pooled-fifo": "pooled intrusive FIFO (full queue)"}
BASELINE_IMPL = "tree"
ROUTING_IMPLS = {"tree-nocache", "ladder-nocache", "ladder-cache1"}
PLACEMENT_COLORS = {"unpinned": BASELINE, "cross_core": SERIES[0], "smt": SERIES[1]}
PLACEMENT_NAMES = {"unpinned": "unpinned", "cross_core": "separate cores", "smt": "SMT siblings, one core"}
BLUES = LinearSegmentedColormap.from_list("blues", ["#eef4fc", "#9cc1ee", "#2a78d6", "#123f7a"])
SCENARIO_ORDER = ["equity", "thin", "deep", "volatile", "wide", "cme6", "multi", "random_ids", "itch"]
QUEUE_IMPLS = ["uncached_index", "ring", "sequenced", "mpmc"]
QUEUE_NAMES = {
    "uncached_index": "ring, uncached counters",
    "ring": "ring, cached counters",
    "sequenced": "ring, per-slot sequence",
    "mpmc": "Vyukov MPMC",
    "mutex": "mutex + condvar",
    "ring_yield": "ring, yield",
    "ring_spin": "ring, spin",
    "ring_backoff": "ring, back-off",
}

plt.rcParams.update(
    {
        "figure.facecolor": SURFACE,
        "axes.facecolor": SURFACE,
        "axes.edgecolor": GRID,
        "axes.labelcolor": MUTED,
        "axes.titlecolor": INK,
        "axes.titlesize": 10,
        "axes.titleweight": "bold",
        "axes.titlepad": 14,
        "xtick.color": MUTED,
        "ytick.color": MUTED,
        "font.size": 9,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "legend.frameon": False,
    }
)


def read(path):
    if not path.exists():
        return []
    with path.open() as f:
        return [row for row in csv.DictReader(f)]


def median(values):
    return statistics.median(values) if values else float("nan")


def ordered(names, order):
    known = [n for n in order if n in names]
    return known + sorted(n for n in names if n not in order)


def style_value_axis(ax, axis="x"):
    ax.grid(axis=axis, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def nice_ns(ns):
    if ns >= 1e6:
        return f"{ns / 1e6:.1f} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:.1f} µs"
    return f"{ns:.0f} ns"


def save(fig, out, name):
    path = out / name
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"wrote {path}")


def book_impl_order(rows):
    by_impl = defaultdict(list)
    for row in rows:
        by_impl[row["impl"]].append(float(row["ns_per_op"]))
    return sorted(by_impl, key=lambda impl: -median(by_impl[impl]))


def plot_book_heatmap(rows, out):
    cells = defaultdict(list)
    for row in rows:
        cells[(row["impl"], row["scenario"])].append(float(row["ns_per_op"]))
    scenarios = ordered({s for _, s in cells}, SCENARIO_ORDER)
    impls = book_impl_order(rows)
    speedup = []
    for impl in impls:
        line = []
        for scenario in scenarios:
            base = median(cells.get((BASELINE_IMPL, scenario), []))
            line.append(base / median(cells[(impl, scenario)]) if (impl, scenario) in cells else float("nan"))
        speedup.append(line)

    fig, ax = plt.subplots(figsize=(1.25 * len(scenarios) + 2.6, 0.48 * len(impls) + 1.4))
    top = max(max(v for v in line if v == v) for line in speedup)
    ax.imshow(speedup, cmap=BLUES, vmin=0.8, vmax=max(top, 1.5), aspect="auto")
    for i, impl in enumerate(impls):
        for j, scenario in enumerate(scenarios):
            if (impl, scenario) not in cells:
                ax.text(j, i, "skipped", ha="center", va="center", color=MUTED, fontsize=7)
                continue
            ns = median(cells[(impl, scenario)])
            dark = speedup[i][j] > 0.62 * max(top, 1.5)
            ax.text(j, i, f"{ns:.0f} ns\n{speedup[i][j]:.1f}x", ha="center", va="center", fontsize=7.5,
                    color="#ffffff" if dark else INK)
    ax.set_xticks(range(len(scenarios)), scenarios)
    ax.set_yticks(range(len(impls)), impls)
    ax.tick_params(length=0)
    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.set_title(f"Book: median ns per operation, and speed-up over {BASELINE_IMPL} (darker = faster)", loc="left")
    save(fig, out, "book_heatmap.png")


def plot_book_bars(rows, out, scenario):
    rows = [r for r in rows if r["scenario"] == scenario and r["impl"] not in ROUTING_IMPLS]
    if not rows:
        return
    by_impl = defaultdict(list)
    meta = {}
    for row in rows:
        by_impl[row["impl"]].append(float(row["ns_per_op"]))
        meta[row["impl"]] = row
    impls = sorted(by_impl, key=lambda impl: -median(by_impl[impl]))
    fig, ax = plt.subplots(figsize=(7.5, 0.36 * len(impls) + 1.2))
    for i, impl in enumerate(impls):
        values = by_impl[impl]
        color = ORDER_COLORS.get(meta[impl]["orders"], BASELINE)
        ax.barh(i, median(values), height=0.62, color=color, edgecolor=SURFACE, linewidth=2)
        ax.plot([min(values), max(values)], [i, i], color=INK, linewidth=1)
        ax.text(max(values) * 1.02, i, f"{median(values):.0f} ns", va="center", color=INK, fontsize=8)
    ax.set_yticks(range(len(impls)), impls)
    ax.set_xlabel("ns per operation (bar = median, line = min to max across runs)")
    style_value_axis(ax)
    kinds = [k for k in ORDER_COLORS if any(meta[i]["orders"] == k for i in impls)]
    handles = [plt.Rectangle((0, 0), 1, 1, color=ORDER_COLORS[k]) for k in kinds]
    ax.legend(handles, [ORDER_NAMES[k] for k in kinds], loc="upper left", bbox_to_anchor=(0, -0.16), ncol=2)
    ax.set_xlim(0, max(max(v) for v in by_impl.values()) * 1.15)
    ax.set_title(f"Book, scenario '{scenario}': lower is faster", loc="left")
    save(fig, out, f"book_{scenario}.png")


def plot_book_latency(rows, out, scenario):
    rows = [r for r in rows if r["scenario"] == scenario]
    if not rows:
        return
    ops = [op for op in ["add", "execute", "cancel", "delete", "replace", "all"] if any(r["op"] == op for r in rows)]
    cells = defaultdict(lambda: defaultdict(list))
    for row in rows:
        for key in ("p50_ns", "p99_ns", "p999_ns"):
            cells[(row["impl"], row["op"])][key].append(float(row[key]))
    impls = sorted({r["impl"] for r in rows}, key=lambda impl: -median(cells[(impl, "all")]["p50_ns"]))
    fig, axes = plt.subplots(1, len(ops), figsize=(2.3 * len(ops) + 1.5, 0.34 * len(impls) + 1.6), sharey=True)
    marks = [("p50_ns", "p50", SERIES[0], "o"), ("p99_ns", "p99", SERIES[1], "s"), ("p999_ns", "p99.9", SERIES[2], "D")]
    for ax, op in zip(axes, ops):
        for i, impl in enumerate(impls):
            values = [median(cells[(impl, op)][key]) for key, *_ in marks]
            ax.plot([values[0], values[-1]], [i, i], color=GRID, linewidth=2, zorder=1)
            for (key, label, color, marker), value in zip(marks, values):
                ax.scatter(value, i, color=color, marker=marker, s=34, zorder=2, edgecolors=SURFACE, linewidths=1.2,
                           label=label if i == 0 else None)
        ax.set_xscale("log")
        ax.xaxis.set_major_locator(matplotlib.ticker.LogLocator(base=10, subs=(1.0,)))
        ax.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: nice_ns(v)))
        ax.xaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
        ax.set_title(op, loc="left")
        style_value_axis(ax)
    axes[0].set_yticks(range(len(impls)), impls)
    axes[0].legend(loc="upper left", bbox_to_anchor=(0, -0.12), ncol=3)
    fig.suptitle(f"Book, scenario '{scenario}': per-operation latency in ns, log scale (includes the BBO read)",
                 x=0.01, ha="left", color=INK, fontweight="bold", fontsize=10)
    save(fig, out, f"book_latency_{scenario}.png")


def perf_per_op(rows, key_fields, count_field):
    values = defaultdict(dict)
    for row in rows:
        if not row["value"]:
            continue
        key = tuple(row[f] for f in key_fields)
        values[key][row["event"]] = float(row["value"])
        values[key]["_count"] = float(row[count_field])
    table = {}
    for key, events in values.items():
        n = events["_count"]
        derived = {}
        if "cycles" in events:
            derived["cycles / op"] = events["cycles"] / n
        if "instructions" in events and "cycles" in events:
            derived["IPC"] = events["instructions"] / events["cycles"]
        for event, label in [("L1-dcache-load-misses", "L1d misses / op"), ("LLC-load-misses", "LLC misses / op"),
                             ("branch-misses", "branch misses / op"), ("dTLB-load-misses", "dTLB misses / op"),
                             ("mem_load_l3_hit_retired.xsnp_hitm", "HITM loads / op"),
                             ("context-switches", "context switches / M ops")]:
            if event in events:
                scale = 1e6 if event == "context-switches" else 1
                derived[label] = events[event] / n * scale
        table[key] = derived
    return table


def plot_book_perf(rows, out):
    if not rows:
        return
    table = perf_per_op(rows, ("scenario", "impl"), "ops")
    scenarios = ordered({s for s, _ in table}, SCENARIO_ORDER)
    metrics = [m for m in ["cycles / op", "IPC", "L1d misses / op", "LLC misses / op", "branch misses / op",
                           "dTLB misses / op"] if any(m in d for d in table.values())]
    impls = sorted({i for _, i in table}, key=lambda impl: -table.get((scenarios[0], impl), {}).get("cycles / op", 0))
    fig, axes = plt.subplots(len(scenarios), len(metrics), figsize=(2.4 * len(metrics) + 1.5,
                                                                     (0.3 * len(impls) + 0.8) * len(scenarios)),
                             sharey=True, squeeze=False)
    for r, scenario in enumerate(scenarios):
        for c, metric in enumerate(metrics):
            ax = axes[r][c]
            for i, impl in enumerate(impls):
                value = table.get((scenario, impl), {}).get(metric)
                if value is None:
                    continue
                ax.barh(i, value, height=0.62, color=BASELINE if impl == BASELINE_IMPL else SERIES[0])
                ax.text(value, i, f" {value:.2f}" if value < 10 else f" {value:.0f}", va="center", fontsize=7, color=INK)
            style_value_axis(ax)
            if r == 0:
                ax.set_title(metric, loc="left")
            if c == 0:
                ax.set_ylabel(scenario, color=INK, fontweight="bold")
    axes[0][0].set_yticks(range(len(impls)), impls)
    fig.suptitle("Book: hardware counters per operation (timed loop only)", x=0.01, ha="left", color=INK,
                 fontweight="bold", fontsize=10)
    fig.tight_layout()
    save(fig, out, "book_perf.png")
    return table, scenarios, metrics, impls


def book_summary(throughput, perf, out):
    lines = ["# Book benchmark summary", ""]
    env = out / "book_env.txt"
    if env.exists():
        lines += ["```", env.read_text().strip(), "```", ""]
    cells = defaultdict(list)
    for row in throughput:
        cells[(row["impl"], row["scenario"])].append(float(row["ns_per_op"]))
    scenarios = ordered({s for _, s in cells}, SCENARIO_ORDER)
    impls = book_impl_order(throughput)[::-1]
    lines.append("Median ns per operation (each op includes one best bid/ask read):")
    lines.append("")
    lines.append("| impl | " + " | ".join(scenarios) + " |")
    lines.append("|---" * (len(scenarios) + 1) + "|")
    for impl in impls:
        lines.append(f"| {impl} | " + " | ".join(
            f"{median(cells[(impl, s)]):.1f}" if (impl, s) in cells else "-" for s in scenarios) + " |")
    if perf:
        table, perf_scenarios, metrics, perf_impls = perf
        for scenario in perf_scenarios:
            lines += ["", f"Hardware counters per operation, scenario `{scenario}`:", "",
                      "| impl | " + " | ".join(metrics) + " |", "|---" * (len(metrics) + 1) + "|"]
            for impl in perf_impls[::-1]:
                d = table.get((scenario, impl), {})
                lines.append(f"| {impl} | " + " | ".join(f"{d[m]:.2f}" if m in d else "-" for m in metrics) + " |")
    (out / "book_summary.md").write_text("\n".join(lines) + "\n")
    print(f"wrote {out / 'book_summary.md'}")


def plot_book_routing(rows, out):
    impls = ["tree-nocache", "tree", "ladder-nocache", "ladder-cache1", "ladder"]
    rows = [r for r in rows if r["impl"] in impls]
    if not rows:
        return
    scenarios = ordered({r["scenario"] for r in rows}, SCENARIO_ORDER)
    cells = defaultdict(list)
    for row in rows:
        cells[(row["impl"], row["scenario"])].append(float(row["ns_per_op"]))
    names = {"tree-nocache": "tree, no cache", "tree": "tree, 64-slot cache", "ladder-nocache": "ladder, no cache",
             "ladder-cache1": "ladder, 1-slot cache", "ladder": "ladder, 64-slot cache"}
    colors = {"tree-nocache": "#b8b7b1", "tree": BASELINE, "ladder-nocache": "#9cc1ee", "ladder-cache1": SERIES[2],
              "ladder": SERIES[0]}
    present = [i for i in impls if any(k[0] == i for k in cells)]
    fig, ax = plt.subplots(figsize=(1.3 * len(scenarios) + 2, 3.8))
    grouped_bars(ax, scenarios, present, lambda g, s: cells.get((s, g), []), colors, names, lambda v: f"{v:.0f}",
                 {s: s for s in scenarios})
    ax.set_ylabel("ns per operation")
    ax.legend(loc="upper left", bbox_to_anchor=(0, -0.28), ncol=3)
    ax.set_title("Book: instrument lookup in BasicBook::book_for (lower is faster)", loc="left")
    save(fig, out, "book_routing.png")


def book(out):
    throughput = read(out / "book_throughput.csv")
    latency = read(out / "book_latency.csv")
    perf = read(out / "book_perf.csv")
    if not throughput:
        sys.exit(f"no book_throughput.csv in {out}")
    plot_book_heatmap(throughput, out)
    for scenario in ordered({r["scenario"] for r in throughput}, SCENARIO_ORDER):
        plot_book_bars(throughput, out, scenario)
    for scenario in [s for s in ("equity", "deep") if any(r["scenario"] == s for r in latency)]:
        plot_book_latency(latency, out, scenario)
    plot_book_routing(throughput, out)
    perf_table = plot_book_perf(perf, out)
    book_summary(throughput, perf_table, out)


def grouped_bars(ax, groups, series, value_of, colors, names, unit_fmt, group_names=None):
    group_names = group_names or QUEUE_NAMES
    width = 0.8 / len(series)
    for s, name in enumerate(series):
        for g, group in enumerate(groups):
            values = value_of(group, name)
            if not values:
                continue
            x = g + (s - (len(series) - 1) / 2) * width
            ax.bar(x, median(values), width=width * 0.92, color=colors[name], label=names[name] if g == 0 else None)
            if len(values) > 1:
                ax.plot([x, x], [min(values), max(values)], color=INK, linewidth=1)
            ax.text(x, max(values), unit_fmt(median(values)), ha="center", va="bottom", fontsize=6.5, color=INK,
                    rotation=90 if len(series) > 2 or len(groups) > 3 else 0)
    ax.set_xticks(range(len(groups)), [group_names.get(g, g) for g in groups], rotation=25, ha="right")
    ax.margins(y=0.18)
    style_value_axis(ax, "y")


def queue(out):
    buffers = read(out / "queue_buffers.csv")
    idle = read(out / "queue_idle.csv")
    idle_latency = read(out / "queue_idle_latency.csv")
    perf = read(out / "queue_perf.csv")
    lines = ["# Queue benchmark summary", ""]
    env = out / "queue_env.txt"
    if env.exists():
        lines += ["```", env.read_text().strip(), "```", ""]

    if buffers:
        placements = [p for p in PLACEMENT_COLORS if any(r["placement"] == p for r in buffers)]
        impls = [i for i in QUEUE_IMPLS if any(r["impl"] == i for r in buffers)]
        flat = defaultdict(list)
        paced = defaultdict(lambda: defaultdict(list))
        for row in buffers:
            if row["scenario"] == "flat":
                flat[(row["impl"], row["placement"])].append(float(row["throughput_mps"]))
            else:
                for key in ("p50_ns", "p99_ns", "consumer_cpu_pct"):
                    paced[(row["impl"], row["placement"], int(row["scenario"]))][key].append(float(row[key]))
        fig, ax = plt.subplots(figsize=(8, 3.6))
        grouped_bars(ax, impls, placements, lambda g, s: flat.get((g, s), []), PLACEMENT_COLORS, PLACEMENT_NAMES,
                     lambda v: f"{v:.0f}")
        ax.set_ylabel("M items / s, flat out (higher is better)")
        ax.legend(loc="upper left", bbox_to_anchor=(0, -0.3), ncol=len(placements))
        ax.set_title("Ring buffers: throughput by thread placement (bar = median, line = min to max)", loc="left")
        save(fig, out, "queue_buffers.png")

        rates = sorted({k[2] for k in paced})
        fig, axes = plt.subplots(2, len(rates), figsize=(4 * len(rates), 6), squeeze=False)
        for c, rate in enumerate(rates):
            for r, key in enumerate(("p50_ns", "p99_ns")):
                ax = axes[r][c]
                grouped_bars(ax, impls, placements, lambda g, s: paced[(g, s, rate)][key], PLACEMENT_COLORS,
                             PLACEMENT_NAMES, lambda v: nice_ns(v))
                ax.set_yscale("log")
                ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: nice_ns(v)))
                ax.yaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
                ax.set_title(f"{key[:-3]} at {rate:,} msgs/s", loc="left")
        fig.legend(*axes[0][0].get_legend_handles_labels(), loc="lower left", bbox_to_anchor=(0.01, -0.04),
                   ncol=len(placements))
        axes[0][0].set_ylabel("ns, log scale")
        axes[1][0].set_ylabel("ns, log scale")
        fig.suptitle("Ring buffers: handoff latency by thread placement (lower is better)", x=0.01, ha="left",
                     color=INK, fontweight="bold", fontsize=10)
        fig.tight_layout()
        save(fig, out, "queue_buffers_latency.png")

        lines += ["Buffers, flat out, median M items/s:", "", "| buffer | " + " | ".join(placements) + " |",
                  "|---" * (len(placements) + 1) + "|"]
        for impl in impls:
            lines.append(f"| {QUEUE_NAMES[impl]} | " + " | ".join(
                f"{median(flat[(impl, p)]):.0f}" if flat.get((impl, p)) else "-" for p in placements) + " |")
        for rate in rates:
            lines += ["", f"Buffers paced at {rate:,} msgs/s, median p50 / p99 ns:", "",
                      "| buffer | " + " | ".join(placements) + " |", "|---" * (len(placements) + 1) + "|"]
            for impl in impls:
                cells = []
                for p in placements:
                    d = paced.get((impl, p, rate))
                    cells.append(f"{median(d['p50_ns']):.0f} / {median(d['p99_ns']):.0f}" if d else "-")
                lines.append(f"| {QUEUE_NAMES[impl]} | " + " | ".join(cells) + " |")

    if idle or idle_latency:
        flat = defaultdict(list)
        for row in idle:
            flat[(row["impl"], row["placement"])].append(float(row["size"]) / float(row["ms"]) / 1000.0)
        lat = defaultdict(lambda: defaultdict(list))
        for row in idle_latency:
            for key in ("p50_ns", "p99_ns", "consumer_cpu_pct"):
                lat[(row["impl"], row["placement"], int(row["rate"]))][key].append(float(row[key]))
        placements = [p for p in PLACEMENT_COLORS if any(r["placement"] == p for r in idle + idle_latency)]
        impls = [i for i in ["mutex", "ring", "ring_spin", "ring_backoff"] if any(r["impl"] == i for r in idle + idle_latency)]
        names = dict(QUEUE_NAMES, ring="ring, yield")
        rates = sorted({k[2] for k in lat})
        fig, axes = plt.subplots(1, 1 + 2 * len(rates), figsize=(3.3 * (1 + 2 * len(rates)), 3.8), squeeze=False)
        axes = axes[0]
        grouped_bars(axes[0], impls, placements, lambda g, s: flat.get((g, s), []), PLACEMENT_COLORS, PLACEMENT_NAMES,
                     lambda v: f"{v:.0f}", names)
        axes[0].set_title("flat out, M items/s", loc="left")
        fig.legend(*axes[0].get_legend_handles_labels(), loc="lower left", bbox_to_anchor=(0.01, -0.08),
                   ncol=len(placements))
        for k, rate in enumerate(rates):
            ax = axes[1 + 2 * k]
            grouped_bars(ax, impls, placements, lambda g, s: lat[(g, s, rate)]["p99_ns"], PLACEMENT_COLORS,
                         PLACEMENT_NAMES, nice_ns, names)
            ax.set_yscale("log")
            ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: nice_ns(v)))
            ax.yaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
            ax.set_title(f"p99 at {rate:,}/s", loc="left")
            ax = axes[2 + 2 * k]
            grouped_bars(ax, impls, placements, lambda g, s: lat[(g, s, rate)]["consumer_cpu_pct"], PLACEMENT_COLORS,
                         PLACEMENT_NAMES, lambda v: f"{v:.0f}%", names)
            ax.set_title(f"consumer CPU at {rate:,}/s", loc="left")
        fig.suptitle("Idle strategies (cached-counter ring vs mutex)", x=0.01, ha="left", color=INK,
                     fontweight="bold", fontsize=10)
        fig.tight_layout()
        save(fig, out, "queue_idle.png")

    if perf:
        table = perf_per_op(perf, ("placement", "impl"), "items")
        placements = [p for p in PLACEMENT_COLORS if any(k[0] == p for k in table)]
        impls = [i for i in ["mutex", "ring_yield", "ring_spin", "ring", "uncached_index", "sequenced", "mpmc"]
                 if any(k[1] == i for k in table)]
        metrics = [m for m in ["cycles / op", "IPC", "L1d misses / op", "LLC misses / op", "HITM loads / op",
                               "context switches / M ops"] if any(m in d for d in table.values())]
        fig, axes = plt.subplots(1, len(metrics), figsize=(3.2 * len(metrics), 0.32 * len(impls) + 1.8), sharey=True,
                                 squeeze=False)
        height = 0.8 / len(placements)
        for ax, metric in zip(axes[0], metrics):
            for p_i, placement in enumerate(placements):
                for i, impl in enumerate(impls):
                    value = table.get((placement, impl), {}).get(metric)
                    if value is None:
                        continue
                    y = i + (p_i - (len(placements) - 1) / 2) * height
                    ax.barh(y, value, height=height * 0.9, color=PLACEMENT_COLORS[placement],
                            label=PLACEMENT_NAMES[placement] if i == 0 else None)
            ax.set_title(metric + " (item)", loc="left")
            style_value_axis(ax)
        axes[0][0].set_yticks(range(len(impls)), [QUEUE_NAMES.get(i, i) for i in impls])
        axes[0][0].legend(loc="upper left", bbox_to_anchor=(0, -0.08), ncol=len(placements))
        fig.suptitle("Queues: hardware counters per item handed off (whole process, both threads)", x=0.01, ha="left",
                     color=INK, fontweight="bold", fontsize=10)
        fig.tight_layout()
        save(fig, out, "queue_perf.png")
        lines += ["", "Hardware counters per item:", "", "| placement | queue | " + " | ".join(metrics) + " |",
                  "|---" * (len(metrics) + 2) + "|"]
        for placement in placements:
            for impl in impls:
                d = table.get((placement, impl), {})
                lines.append(f"| {placement} | {QUEUE_NAMES.get(impl, impl)} | " + " | ".join(
                    f"{d[m]:.3f}" if m in d else "-" for m in metrics) + " |")

    (out / "queue_summary.md").write_text("\n".join(lines) + "\n")
    print(f"wrote {out / 'queue_summary.md'}")


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("book", "queue"):
        sys.exit("usage: plot_linux_bench.py book|queue <results dir>")
    out = Path(sys.argv[2])
    (book if sys.argv[1] == "book" else queue)(out)


if __name__ == "__main__":
    main()
