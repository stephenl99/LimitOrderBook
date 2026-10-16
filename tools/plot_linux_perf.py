#!/usr/bin/env python3
import csv
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

INK = "#0b0b0b"
MUTED = "#52514e"
GRID = "#e4e3de"
SURFACE = "#fcfcfb"
BASELINE = "#8a8984"
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948"]
BLUE_STEPS = ["#c7dbf5", "#7aaae6", "#2a78d6", "#123f7a"]
ORDER_COLORS = {"list": BASELINE, "aggregate": SERIES[0], "pooled-fifo": SERIES[1], "lazy-fifo": SERIES[2]}
ORDER_NAMES = {"list": "std::list queue", "aggregate": "aggregate (totals only)", "pooled-fifo": "pooled FIFO queue",
               "lazy-fifo": "lazy FIFO queue"}
SCENARIO_ORDER = ["cme_mix", "esz6", "equity", "deep", "multi", "cme6", "random_ids"]
CATEGORY_ORDER = ["order index", "price levels", "order queue", "book logic", "instrument lookup", "std::vector access",
                  "benchmark harness", "kernel", "other"]
CATEGORY_COLORS = dict(zip(CATEGORY_ORDER, SERIES[:6] + [BASELINE, "#b8b7b1", "#d9d8d2"]))

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "axes.edgecolor": GRID, "axes.labelcolor": MUTED,
    "axes.titlecolor": INK, "axes.titlesize": 10, "axes.titleweight": "bold", "axes.titlepad": 10,
    "xtick.color": MUTED, "ytick.color": MUTED, "font.size": 9, "axes.spines.top": False,
    "axes.spines.right": False, "legend.frameon": False,
})


def read(path):
    if not path.exists():
        return []
    with path.open() as f:
        return list(csv.DictReader(f))


def save(fig, out, name):
    fig.savefig(out / name, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"wrote {out / name}")


def grid_x(ax):
    ax.grid(axis="x", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def ordered(names, order):
    return [n for n in order if n in names] + sorted(n for n in names if n not in order)


def variant_meta():
    meta = {}
    root = Path(__file__).resolve().parent.parent
    header = root / "Core/Source/BookVariants/BookVariants.h"
    if header.exists():
        for match in re.finditer(r'BookVariantInfo\{"([^"]+)", "([^"]+)", "([^"]+)", "([^"]+)", (\w+)\}', header.read_text()):
            meta[match.group(1)] = {"levels": match.group(2), "index": match.group(3), "orders": match.group(4)}
    return meta


def book_table(rows):
    table = defaultdict(dict)
    ops = {}
    for row in rows:
        if not row["value"]:
            continue
        key = (row["scenario"], row["impl"])
        table[key][(row["group"], row["event"])] = float(row["value"])
        ops[key] = float(row["ops"])
    metrics = {}
    for key, events in table.items():
        n = ops[key]
        get = lambda group, event: events.get((group, event))
        m = {}
        cycles = get("basic", "cycles")
        instructions = get("basic", "instructions")
        if cycles and instructions:
            m["cycles/op"] = cycles / n
            m["instructions/op"] = instructions / n
            m["IPC"] = instructions / cycles
            task_ms = get("basic", "task-clock")
            if task_ms:
                m["GHz"] = cycles / (task_ms * 1e6)
            if get("basic", "branch-misses"):
                m["branch misses/op"] = get("basic", "branch-misses") / n
        slots_cycles = get("slots", "cycles")
        frontend_cycles = get("frontend", "cycles")
        if slots_cycles and frontend_cycles:
            issued = get("slots", "uops_issued.any") / slots_cycles
            retired = get("slots", "uops_retired.retire_slots") / slots_cycles
            not_delivered = get("frontend", "idq_uops_not_delivered.core") / frontend_cycles
            recovery = get("frontend", "int_misc.recovery_cycles_any") / frontend_cycles
            retiring = retired / 4
            frontend = not_delivered / 4
            bad = max(0.0, (issued - retired + 4 * recovery) / 4)
            backend = max(0.0, 1 - retiring - frontend - bad)
            m["topdown"] = {"retiring": retiring, "frontend bound": frontend, "bad speculation": bad,
                            "backend bound": backend}
        for event, label in (("mem_load_retired.l1_miss", "L1 misses/op"), ("mem_load_retired.l2_miss", "L2 misses/op"),
                             ("mem_load_retired.l3_miss", "L3 misses/op")):
            if get("misses", event) is not None:
                m[label] = get("misses", event) / n
        stall_cycles = get("stalls", "cycles")
        if stall_cycles:
            for event, label in (("cycle_activity.stalls_l1d_miss", "stalled on L1d miss"),
                                 ("cycle_activity.stalls_l2_miss", "stalled on L2 miss"),
                                 ("cycle_activity.stalls_l3_miss", "stalled on L3 miss")):
                if get("stalls", event) is not None:
                    m[label] = get("stalls", event) / stall_cycles
        metrics[key] = m
    return metrics


def impl_order(metrics, scenario):
    impls = [impl for (s, impl) in metrics if s == scenario]
    return sorted(impls, key=lambda impl: -metrics[(scenario, impl)].get("cycles/op", 0))


def plot_topdown(metrics, meta, out):
    scenarios = [s for s in ordered({s for s, _ in metrics}, SCENARIO_ORDER) if any(
        "topdown" in m for (sc, _), m in metrics.items() if sc == s)][:3]
    if not scenarios:
        return
    parts = ["retiring", "bad speculation", "frontend bound", "backend bound"]
    colors = {"retiring": SERIES[2], "bad speculation": SERIES[7], "frontend bound": SERIES[3], "backend bound": SERIES[0]}
    impls = impl_order(metrics, scenarios[0])
    fig, axes = plt.subplots(1, len(scenarios), figsize=(4.2 * len(scenarios) + 1.6, 0.34 * len(impls) + 1.8),
                             sharey=True, squeeze=False)
    for ax, scenario in zip(axes[0], scenarios):
        for i, impl in enumerate(impls):
            td = metrics.get((scenario, impl), {}).get("topdown")
            if not td:
                continue
            left = 0.0
            for part in parts:
                ax.barh(i, td[part], left=left, height=0.66, color=colors[part], edgecolor=SURFACE, linewidth=1.5,
                        label=part if i == 0 else None)
                if td[part] >= 0.08:
                    ax.text(left + td[part] / 2, i, f"{100 * td[part]:.0f}", ha="center", va="center", fontsize=7,
                            color="#ffffff" if part in ("backend bound", "bad speculation") else INK)
                left += td[part]
        ax.set_xlim(0, 1)
        ax.xaxis.set_major_formatter(matplotlib.ticker.PercentFormatter(1.0))
        ax.set_title(scenario, loc="left")
        grid_x(ax)
    axes[0][0].set_yticks(range(len(impls)), impls)
    fig.legend(*axes[0][0].get_legend_handles_labels(), loc="lower left", bbox_to_anchor=(0.01, -0.06), ncol=4)
    fig.suptitle("Where each book's cycles go (Intel top-down, level 1, % of pipeline slots)", x=0.01, ha="left",
                 color=INK, fontweight="bold", fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    save(fig, out, "book_topdown.png")


def plot_work(metrics, meta, out):
    scenarios = ordered({s for s, _ in metrics}, SCENARIO_ORDER)[:3]
    columns = ["cycles/op", "instructions/op", "IPC", "branch misses/op"]
    impls = impl_order(metrics, scenarios[0])
    fig, axes = plt.subplots(len(scenarios), len(columns), figsize=(3.1 * len(columns) + 1.6,
                                                                    (0.3 * len(impls) + 0.9) * len(scenarios)),
                             sharey=True, squeeze=False)
    for r, scenario in enumerate(scenarios):
        for c, column in enumerate(columns):
            ax = axes[r][c]
            for i, impl in enumerate(impls):
                value = metrics.get((scenario, impl), {}).get(column)
                if value is None:
                    continue
                color = ORDER_COLORS.get(meta.get(impl, {}).get("orders"), BASELINE)
                ax.barh(i, value, height=0.66, color=color)
                ax.text(value, i, f" {value:.2f}" if value < 10 else f" {value:.0f}", va="center", fontsize=7, color=INK)
            ax.margins(x=0.22)
            grid_x(ax)
            if r == 0:
                ax.set_title(column, loc="left")
            if c == 0:
                ax.set_ylabel(scenario, color=INK, fontweight="bold")
    axes[0][0].set_yticks(range(len(impls)), impls)
    kinds = [k for k in ORDER_COLORS if any(meta.get(i, {}).get("orders") == k for i in impls)]
    fig.legend([plt.Rectangle((0, 0), 1, 1, color=ORDER_COLORS[k]) for k in kinds], [ORDER_NAMES[k] for k in kinds],
               loc="lower left", bbox_to_anchor=(0.01, -0.03), ncol=len(kinds))
    ghz = [m["GHz"] for m in metrics.values() if "GHz" in m]
    clock = f"; clock during these runs {min(ghz):.2f} to {max(ghz):.2f} GHz" if ghz else ""
    fig.suptitle(f"Work per operation, independent of clock speed{clock}", x=0.01, ha="left", color=INK,
                 fontweight="bold", fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    save(fig, out, "book_work.png")


def plot_misses(metrics, meta, out):
    scenarios = [s for s in ordered({s for s, _ in metrics}, SCENARIO_ORDER) if any(
        "L1 misses/op" in m for (sc, _), m in metrics.items() if sc == s)][:3]
    if not scenarios:
        return
    impls = impl_order(metrics, scenarios[0])
    levels = [("L1 misses/op", BLUE_STEPS[0]), ("L2 misses/op", BLUE_STEPS[2]), ("L3 misses/op", BLUE_STEPS[3])]
    stalls = [("stalled on L1d miss", BLUE_STEPS[0]), ("stalled on L2 miss", BLUE_STEPS[2]),
              ("stalled on L3 miss", BLUE_STEPS[3])]
    fig, axes = plt.subplots(2, len(scenarios), figsize=(4.0 * len(scenarios) + 1.6, 2 * (0.32 * len(impls) + 1.2)),
                             sharey=True, squeeze=False)
    for c, scenario in enumerate(scenarios):
        ax = axes[0][c]
        height = 0.8 / len(levels)
        for k, (label, color) in enumerate(levels):
            for i, impl in enumerate(impls):
                value = metrics.get((scenario, impl), {}).get(label)
                if value is None:
                    continue
                ax.barh(i + (k - 1) * height, value, height=height * 0.92, color=color,
                        label=label.replace("/op", "") if i == 0 else None)
        ax.set_title(f"{scenario}: loads that missed, per op", loc="left")
        grid_x(ax)
        ax = axes[1][c]
        for label, color in stalls:
            for i, impl in enumerate(impls):
                value = metrics.get((scenario, impl), {}).get(label)
                if value is None:
                    continue
                ax.barh(i, value, height=0.66, color=color, label=label if i == 0 else None)
            if label == "stalled on L3 miss":
                for i, impl in enumerate(impls):
                    value = metrics.get((scenario, impl), {}).get(label)
                    if value is not None:
                        ax.text(value, i, f" {100 * value:.0f}%", va="center", fontsize=7, color=INK)
        ax.set_xlim(0, 1)
        ax.xaxis.set_major_formatter(matplotlib.ticker.PercentFormatter(1.0))
        ax.set_title(f"{scenario}: % of cycles stalled waiting on a miss", loc="left")
        grid_x(ax)
    for r in range(2):
        axes[r][0].set_yticks(range(len(impls)), impls)
        axes[r][-1].legend(loc="lower right", fontsize=8)
    fig.suptitle("Cache misses and the stalls they cause (bars nest: L3 within L2 within L1)", x=0.01, ha="left",
                 color=INK, fontweight="bold", fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    save(fig, out, "book_misses.png")


def plot_mem_sources(rows, out):
    if not rows:
        return
    totals = defaultdict(float)
    for row in rows:
        totals[(row["scenario"], row["impl"], row["category"])] += float(row["latency_cycles"])
    scenarios = ordered({s for s, _, _ in totals}, SCENARIO_ORDER)
    impls = list(dict.fromkeys(row["impl"] for row in rows))
    categories = [c for c in CATEGORY_ORDER if any(k[2] == c for k in totals)]
    fig, axes = plt.subplots(1, len(scenarios), figsize=(4.6 * len(scenarios) + 1.6, 0.42 * len(impls) + 1.9),
                             sharey=True, squeeze=False)
    for ax, scenario in zip(axes[0], scenarios):
        for i, impl in enumerate(impls):
            whole = sum(v for (s, im, _), v in totals.items() if s == scenario and im == impl)
            if not whole:
                continue
            left = 0.0
            for category in categories:
                share = totals.get((scenario, impl, category), 0.0) / whole
                if share <= 0:
                    continue
                ax.barh(i, share, left=left, height=0.66, color=CATEGORY_COLORS[category], edgecolor=SURFACE,
                        linewidth=1.5)
                if share >= 0.09:
                    ax.text(left + share / 2, i, f"{100 * share:.0f}", ha="center", va="center", fontsize=7,
                            color="#ffffff" if category in ("order index", "order queue", "book logic") else INK)
                left += share
        ax.set_xlim(0, 1)
        ax.xaxis.set_major_formatter(matplotlib.ticker.PercentFormatter(1.0))
        ax.set_title(scenario, loc="left")
        grid_x(ax)
    axes[0][0].set_yticks(range(len(impls)), impls)
    handles = [plt.Rectangle((0, 0), 1, 1, color=CATEGORY_COLORS[c]) for c in categories]
    fig.legend(handles, categories, loc="lower left", bbox_to_anchor=(0.01, -0.12), ncol=5)
    fig.suptitle("Which structure the slow loads (>= 30 cycles) belong to, share of their total latency (perf mem)",
                 x=0.01, ha="left", color=INK, fontweight="bold", fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    save(fig, out, "book_mem_sources.png")


def short_symbol(symbol):
    symbol = symbol.replace("(anonymous namespace)::", "")
    previous = None
    while previous != symbol:
        previous = symbol
        symbol = re.sub(r"<[^<>]*>", "", symbol)
    symbol = re.sub(r"\(.*", "", symbol)
    symbol = re.sub(r"^(void|std::\w+|auto|bool|unsigned \w+|\w+) (?=\S)", "", symbol).strip()
    if any(key in symbol for key in ("for_each_book_variant", "run_throughput", "apply", "with_book", "operator()")):
        return "benchmark loop (inlined apply)"
    return symbol[-46:] or "unknown"


def plot_hotspots(rows, out):
    if not rows:
        return
    impls = list(dict.fromkeys(row["impl"] for row in rows))
    fig, axes = plt.subplots(1, len(impls), figsize=(3.6 * len(impls), 3.4), squeeze=False)
    for ax, impl in zip(axes[0], impls):
        merged = defaultdict(float)
        for row in rows:
            if row["impl"] == impl:
                merged[short_symbol(row["symbol"])] += float(row["self_pct"])
        top = sorted(merged.items(), key=lambda kv: -kv[1])[:7][::-1]
        ax.barh(range(len(top)), [v for _, v in top], height=0.66, color=SERIES[0])
        ax.set_yticks(range(len(top)), [name for name, _ in top], fontsize=7)
        for i, (_, value) in enumerate(top):
            ax.text(value, i, f" {value:.0f}%", va="center", fontsize=7, color=INK)
        ax.margins(x=0.25)
        ax.set_title(impl, loc="left")
        grid_x(ax)
    fig.suptitle(f"Self time by function, {rows[0]['scenario']} (perf record, timed loop only)", x=0.01, ha="left", color=INK,
                 fontweight="bold", fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    save(fig, out, "book_hotspots.png")


def plot_queue(rows, out):
    if not rows:
        return
    values = defaultdict(dict)
    for row in rows:
        if row["value"]:
            values[(row["placement"], row["impl"], row["side"])][row["event"]] = float(row["value"]) / float(row["items"])
    impls = [i for i in ["mutex", "ring_yield", "ring_spin", "ring", "uncached_index", "sequenced", "mpmc"]
             if any(k[1] == i for k in values)]
    names = {"mutex": "mutex + condvar", "ring_yield": "cached ring, yield", "ring_spin": "cached ring, spin",
             "ring": "cached ring, back-off", "uncached_index": "uncached ring, back-off",
             "sequenced": "sequence ring, back-off", "mpmc": "MPMC, back-off"}
    series = [("cross_core", "producer"), ("cross_core", "consumer"), ("smt", "producer"), ("smt", "consumer")]
    series = [s for s in series if any(k[0] == s[0] and k[2] == s[1] for k in values)]
    colors = {("cross_core", "producer"): "#7aaae6", ("cross_core", "consumer"): SERIES[0],
              ("smt", "producer"): "#f3a37f", ("smt", "consumer"): SERIES[1]}
    metrics = [("cycles", "cycles per item"), ("mem_load_l3_hit_retired.xsnp_hitm", "HITM loads per item"),
               ("machine_clears.memory_ordering", "memory-ordering clears per item"),
               ("mem_inst_retired.lock_loads", "locked atomics per item")]
    fig, axes = plt.subplots(1, len(metrics), figsize=(3.4 * len(metrics) + 1.6, 0.5 * len(impls) + 2.0), sharey=True,
                             squeeze=False)
    height = 0.8 / len(series)
    for ax, (event, title) in zip(axes[0], metrics):
        for k, key in enumerate(series):
            for i, impl in enumerate(impls):
                value = values.get((key[0], impl, key[1]), {}).get(event)
                if value is None:
                    continue
                ax.barh(i + (k - (len(series) - 1) / 2) * height, value, height=height * 0.92, color=colors[key],
                        label=f"{'separate cores' if key[0] == 'cross_core' else 'SMT siblings'}, {key[1]}"
                        if i == 0 else None)
        ax.set_xscale("symlog", linthresh=0.01 if event != "cycles" else 1)
        ax.set_title(title, loc="left")
        grid_x(ax)
    axes[0][0].set_yticks(range(len(impls)), [names[i] for i in impls])
    axes[0][0].invert_yaxis()
    fig.legend(*axes[0][0].get_legend_handles_labels(), loc="lower left", bbox_to_anchor=(0.01, -0.08), ncol=4)
    fig.suptitle("Queues, counted per CPU so producer and consumer separate (20M items, symlog scale)", x=0.01,
                 ha="left", color=INK, fontweight="bold", fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    save(fig, out, "queue_coherence.png")


def plot_clock(rows, out):
    if not rows:
        return
    times, ghz = [], []
    for row in rows:
        if row["cycles"] and row["ref_cycles"] and float(row["ref_cycles"]) > 0:
            times.append(float(row["time_s"]))
            ghz.append(float(row["cycles"]) / float(row["ref_cycles"]) * float(row["nominal_ghz"]))
    if not ghz:
        return
    fig, ax = plt.subplots(figsize=(8, 3))
    ax.plot(times, ghz, color=SERIES[0], linewidth=2)
    ax.axhline(float(rows[0]["nominal_ghz"]), color=BASELINE, linewidth=1, linestyle="--")
    ax.text(times[-1], float(rows[0]["nominal_ghz"]), "  base clock", va="center", fontsize=8, color=MUTED)
    ax.set_xlabel("seconds into the run")
    ax.set_ylabel("GHz while busy")
    ax.set_ylim(0, max(ghz) * 1.15)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_title(f"Benchmark CPU clock, every book back to back (median {statistics.median(ghz):.2f} GHz)", loc="left")
    save(fig, out, "clock.png")


def summary(metrics, mem_rows, queue_rows, out):
    lines = ["# perf summary", ""]
    env = out / "env.txt"
    if env.exists():
        lines += ["```", env.read_text().strip(), "```", ""]
    for scenario in ordered({s for s, _ in metrics}, SCENARIO_ORDER):
        impls = impl_order(metrics, scenario)[::-1]
        cols = ["cycles/op", "instructions/op", "IPC", "GHz", "L1 misses/op", "L2 misses/op", "L3 misses/op",
                "stalled on L3 miss", "branch misses/op"]
        lines += [f"## {scenario}", "", "| impl | " + " | ".join(cols) + " | retiring | bad spec | frontend | backend |",
                  "|---" * (len(cols) + 5) + "|"]
        for impl in impls:
            m = metrics[(scenario, impl)]
            td = m.get("topdown", {})
            cells = [f"{m[c]:.3f}" if c in m and m[c] < 1 else (f"{m[c]:.1f}" if c in m else "-") for c in cols]
            cells += [f"{100 * td[p]:.0f}%" if p in td else "-" for p in
                      ("retiring", "bad speculation", "frontend bound", "backend bound")]
            lines.append(f"| {impl} | " + " | ".join(cells) + " |")
        lines.append("")
    if mem_rows:
        totals = defaultdict(float)
        for row in mem_rows:
            totals[(row["scenario"], row["impl"], row["category"])] += float(row["latency_cycles"])
        lines += ["## Slow-load latency by structure (perf mem)", "", "| scenario | impl | " + " | ".join(CATEGORY_ORDER) + " |",
                  "|---" * (len(CATEGORY_ORDER) + 2) + "|"]
        for scenario, impl in dict.fromkeys((r["scenario"], r["impl"]) for r in mem_rows):
            whole = sum(v for (s, i, _), v in totals.items() if s == scenario and i == impl) or 1
            lines.append(f"| {scenario} | {impl} | " + " | ".join(
                f"{100 * totals.get((scenario, impl, c), 0) / whole:.0f}%" for c in CATEGORY_ORDER) + " |")
        lines.append("")
    if queue_rows:
        lines += ["## Queues, per item", "", "| placement | queue | side | cycles | HITM | ordering clears | locked atomics | context switches |",
                  "|---|---|---|---|---|---|---|---|"]
        values = defaultdict(dict)
        for row in queue_rows:
            if row["value"]:
                values[(row["placement"], row["impl"], row["side"])][row["event"]] = (float(row["value"]), float(row["items"]))
        for (placement, impl, side), events in values.items():
            per = lambda e: events[e][0] / events[e][1] if e in events else float("nan")
            lines.append(f"| {placement} | {impl} | {side} | {per('cycles'):.1f} | {per('mem_load_l3_hit_retired.xsnp_hitm'):.4f} | "
                         f"{per('machine_clears.memory_ordering'):.4f} | {per('mem_inst_retired.lock_loads'):.4f} | "
                         f"{events.get('context-switches', (0, 1))[0]:.0f} |")
    (out / "summary.md").write_text("\n".join(lines) + "\n")
    print(f"wrote {out / 'summary.md'}")


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: plot_linux_perf.py <benchmarks/linux/perf>")
    out = Path(sys.argv[1])
    meta = variant_meta()
    metrics = book_table(read(out / "book_counters.csv"))
    if metrics:
        plot_topdown(metrics, meta, out)
        plot_work(metrics, meta, out)
        plot_misses(metrics, meta, out)
    mem_rows = read(out / "book_mem.csv")
    plot_mem_sources(mem_rows, out)
    plot_hotspots(read(out / "book_hotspots.csv"), out)
    queue_rows = read(out / "queue_counters.csv")
    plot_queue(queue_rows, out)
    plot_clock(read(out / "clock.csv"), out)
    summary(metrics, mem_rows, queue_rows, out)


if __name__ == "__main__":
    main()
