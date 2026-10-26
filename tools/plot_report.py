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
import matplotlib.ticker

INK, MUTED, GRID, SURFACE, GRAY = "#0b0b0b", "#52514e", "#e4e3de", "#fcfcfb", "#8a8984"
DARK, BLUE, LIGHT_BLUE, ORANGE, GOLD, GREEN, RED = "#2f2f2d", "#2a78d6", "#7fb0ea", "#eb6834", "#eda100", "#1baf7a", "#e34948"
VIOLET, MAGENTA, PALE = "#4a3aa7", "#e87ba4", "#d9d8d2"
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

HANDOFF = [("direct", "direct\ncall", GREEN), ("mutex-cv", "mutex\nqueue", RED), ("spin", "ring buffer,\nspin", ORANGE),
           ("yield", "ring buffer,\nyield", GOLD), ("back-off", "ring buffer,\nback-off", BLUE)]
IDLE = [("spin", "spin", ORANGE), ("yield", "yield", GOLD), ("back-off", "back-off", BLUE),
        ("tuned", "tuned\nback-off", VIOLET)]
BOOK = [("map", "agg-map", "std::map", GRAY), ("vector", "agg-flat", "std::vector", ORANGE), ("ladder", "ladder", "ladder", BLUE)]
SHARING = [(1, "1 order book, cores to itself", 1.0), (2, "2 order books share the 2 cores", 0.62),
           (4, "4 order books share the 2 cores", 0.32)]


def read(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def read_positional(path):
    with open(path) as f:
        rows = list(csv.reader(f))
    header, body = rows[0], rows[1:]
    return header, body


def median(values):
    values = [v for v in values if v == v]
    return statistics.median(values) if values else float("nan")


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


def log_ns_axis(ax, floor, top):
    ax.set_yscale("log")
    ax.set_ylim(floor, top)
    ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: fmt_ns(v)))
    ax.yaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())


def throughput_panel(ax, items, title="throughput, M msgs/s"):
    for i, (_, runs, color) in enumerate(items):
        med = statistics.median(runs)
        ax.bar(i, med, color=color, width=0.68)
        ax.plot([i, i], [min(runs), max(runs)], color=INK, linewidth=1)
        ax.text(i, 0.4, f"{med:.1f}", ha="center", va="bottom", fontsize=11, fontweight="bold", color="white")
    ax.set_ylim(0, max(max(r) for _, r, _ in items) * 1.12)
    style(ax, [label for label, _, _ in items], title)


def latency_panel(ax, labels, values, colors, title, floor=100):
    for i, (v, c) in enumerate(zip(values, colors)):
        if v > 0:
            ax.bar(i, v, color=c, width=0.68)
            ax.text(i, v * 1.12, fmt_ns(v), ha="center", va="bottom", fontsize=11, color=INK)
        else:
            ax.text(i, floor * 1.12, "0", ha="center", va="bottom", fontsize=11, color=INK)
    log_ns_axis(ax, floor, max(values) * 3)
    style(ax, labels, title)


OUT.mkdir(parents=True, exist_ok=True)


def save(fig, name):
    fig.savefig(OUT / name, dpi=150)
    plt.close(fig)
    print(f"wrote {OUT / name}")


combos_tp = read(DATA / "combos_throughput.csv") if (DATA / "combos_throughput.csv").exists() else []
latency_header, latency_body = read_positional(DATA / "combos_latency.csv") if (DATA / "combos_latency.csv").exists() and (DATA / "combos_latency.csv").stat().st_size else ([], [])


def combo_latency_rows():
    if not latency_header:
        return []
    index = {}
    for i, name in enumerate(latency_header):
        index.setdefault(name, i)
    rows = []
    for values in latency_body:
        row = {name: values[i] for name, i in index.items()}
        rows.append(row)
    return rows


combos_lat = combo_latency_rows()


def tp(handler, book="ladder"):
    return [float(r["msgs_per_sec"]) / 1e6 for r in combos_tp
            if r["handler"] == handler and r["book"] == book and r["msgs_per_sec"]]


def lat(handler, book="ladder", key="p50_ns"):
    return median(float(r[key]) for r in combos_lat if r["handler"] == handler and r["book"] == book and r[key])


def cpu_busy(field):
    values = {}
    for part in field.split(";"):
        if "=" in part:
            cpu, value = part.split("=")
            values[cpu] = float(value)
    return values


def plot_handoff():
    if not combos_tp or not combos_lat:
        return
    fig, (a, b) = plt.subplots(1, 2, figsize=(14, 4.8))
    throughput_panel(a, [(label, tp(tag), color) for tag, label, color in HANDOFF])
    latency_panel(b, [label for _, label, _ in HANDOFF], [0.0 if tag == "direct" else lat(tag) for tag, _, _ in HANDOFF],
                  [color for _, _, color in HANDOFF], "median handoff latency, push to pickup (log)")
    fig.tight_layout(w_pad=3)
    save(fig, "01_handoff.png")


def plot_idle():
    path_tp, path_lat = DATA / "idle_throughput.csv", DATA / "idle_latency.csv"
    if not path_tp.exists() or not path_lat.exists() or not path_lat.stat().st_size:
        return
    throughput = read(path_tp)
    header, body = read_positional(path_lat)
    index = {}
    for i, name in enumerate(header):
        index.setdefault(name, i)
    latency = [{name: values[i] for name, i in index.items()} for values in body]

    def tp_values(strategy, books):
        records, seconds = defaultdict(float), defaultdict(float)
        for r in throughput:
            if r["strategy"] == strategy and int(r["books"]) == books and r["seconds"]:
                records[r["run"]] += float(r["records"])
                seconds[r["run"]] = max(seconds[r["run"]], float(r["seconds"]))
        return [records[run] / seconds[run] / 1e6 for run in records]

    def lat_values(strategy, books, key):
        return [float(r[key]) for r in latency if r["strategy"] == strategy and int(r["books"]) == books and r[key]]

    def busy_values(strategy, books):
        result = []
        for r in latency:
            if r["strategy"] == strategy and int(r["books"]) == books and r["book_index"] == "0":
                cores = cpu_busy(r["cpu_busy_pct"])
                if cores:
                    result.append(sum(cores.values()) / len(cores))
        return result

    fig, axes = plt.subplots(1, 4, figsize=(18, 5.2))
    strategies = [tag for tag, _, _ in IDLE]
    colors = {tag: color for tag, _, color in IDLE}
    alphas = {books: alpha for books, _, alpha in SHARING}
    levels = [books for books, _, _ in SHARING]
    width = 0.8 / len(levels)

    def draw(ax, value_of, log, fmt, title, floor=None):
        top = 0.0
        for g, strategy in enumerate(strategies):
            for s, books in enumerate(levels):
                values = value_of(strategy, books)
                if not values:
                    continue
                v = median(values)
                x = g + (s - (len(levels) - 1) / 2) * width
                if log:
                    ax.bar(x, v - floor, bottom=floor, width=width * 0.92, color=colors[strategy], alpha=alphas[books])
                    ax.text(x, v * 1.15, fmt_ns(v), ha="center", va="bottom", fontsize=8.5, color=INK, rotation=90)
                else:
                    ax.bar(x, v, width=width * 0.92, color=colors[strategy], alpha=alphas[books])
                    ax.text(x, v * 1.02, fmt.format(v), ha="center", va="bottom", fontsize=8.5, color=INK, rotation=90)
                top = max(top, v)
        style(ax, [label for _, label, _ in IDLE], title)
        if log:
            log_ns_axis(ax, floor, top * 12)
        else:
            ax.set_ylim(0, top * 1.3)

    draw(axes[0], tp_values, False, "{:.1f}", "throughput, all books combined, M msgs/s")
    draw(axes[1], lambda s, b: lat_values(s, b, "p50_ns"), True, None, "median latency, push to pickup (log)", floor=100)
    draw(axes[2], lambda s, b: lat_values(s, b, "p90_ns"), True, None, "p90 latency, push to pickup (log)", floor=1000)
    draw(axes[3], busy_values, False, "{:.0f}%", "the two cores, % busy")
    axes[3].set_ylim(0, 130)
    axes[3].set_yticks(range(0, 101, 20))
    handles = [plt.Rectangle((0, 0), 1, 1, color=GRAY, alpha=alpha) for _, _, alpha in SHARING]
    fig.legend(handles, [label for _, label, _ in SHARING], loc="lower center", ncol=3, fontsize=11,
               bbox_to_anchor=(0.5, -0.01))
    fig.tight_layout(rect=(0, 0.07, 1, 1), w_pad=2.5)
    save(fig, "02_idle_strategy.png")


def plot_inner_book():
    variants = DATA / "inner_book_variants.csv"
    op_latency = DATA / "inner_book_op_latency.csv"
    if not variants.exists() or not op_latency.exists():
        return
    ns = {r["book"]: float(r["median_ns"]) for r in read(variants)}
    per_op = defaultdict(lambda: defaultdict(list))
    for r in read(op_latency):
        if r["op"] == "all":
            for key in ("p50_ns", "p90_ns", "p99_ns"):
                per_op[r["impl"]][key].append(float(r[key]))
    labels, colors = [label for _, _, label, _ in BOOK], [color for _, _, _, color in BOOK]
    fig, (a, b) = plt.subplots(1, 2, figsize=(12, 4.8), gridspec_kw={"width_ratios": [1, 1.4]})
    mps = [1000 / ns[variant] for _, variant, _, _ in BOOK]
    a.bar(range(3), mps, color=colors, width=0.62)
    for i, v in enumerate(mps):
        a.text(i, v, f"{v:.1f}", ha="center", va="bottom", fontsize=11, color=INK)
    a.set_ylim(0, max(mps) * 1.18)
    style(a, labels, "throughput, M msgs/s")
    width = 0.26
    tops = []
    for k, (column, alpha) in enumerate([("p50_ns", 1.0), ("p90_ns", 0.65), ("p99_ns", 0.35)]):
        vals = [median(per_op[variant][column]) for _, variant, _, _ in BOOK]
        xs = [i + (k - 1) * width for i in range(3)]
        b.bar(xs, vals, width=width * 0.92, color=colors, alpha=alpha)
        for x, v in zip(xs, vals):
            b.text(x, v, f"{v:.0f}", ha="center", va="bottom", fontsize=9.5, color=INK)
        tops.append(max(vals))
    b.set_ylim(0, max(tops) * 1.15)
    style(b, labels, "latency per message, ns (p50 / p90 / p99)")
    fig.tight_layout(w_pad=3)
    save(fig, "03_inner_book.png")


def plot_inner_book_counters():
    counters = DATA / "inner_book_counters.csv"
    loads = DATA / "inner_book_slow_loads.csv"
    op_latency = DATA / "inner_book_op_latency.csv"
    if not counters.exists() or not loads.exists():
        return
    metric, count, ops = defaultdict(dict), defaultdict(dict), {}
    for r in read(counters):
        if not r["value"]:
            continue
        target = metric if r["kind"] == "metric" else count
        target[r["book"]].setdefault(r["name"], float(r["value"]))
        ops[r["book"]] = float(r["ops"])
    fig, axes = plt.subplots(1, 3, figsize=(17, 5.6), gridspec_kw={"width_ratios": [1.15, 1, 1.25]})

    slices = [("useful work (retiring)", GREEN), ("mispredicted branches", RED), ("instruction supply (frontend)", GOLD),
              ("waiting on caches", LIGHT_BLUE), ("waiting on DRAM", BLUE), ("execution units (core)", GRAY)]
    ax = axes[0]
    for i, (_, variant, _, _) in enumerate(BOOK):
        m = metric[variant]
        cycles = count[variant]["cycles"] / ops[variant]
        parts = [m["tma_retiring"], m["tma_bad_speculation"], m["tma_frontend_bound"], m["tma_memory_bound"],
                 m["tma_core_bound"]]
        scale = cycles / sum(parts)
        memory_levels = [m[k] for k in ("tma_l1_bound", "tma_l2_bound", "tma_l3_bound", "tma_dram_bound", "tma_store_bound")]
        dram_share = m["tma_dram_bound"] / sum(memory_levels) if sum(memory_levels) else 0
        values = [m["tma_retiring"], m["tma_bad_speculation"], m["tma_frontend_bound"],
                  m["tma_memory_bound"] * (1 - dram_share), m["tma_memory_bound"] * dram_share, m["tma_core_bound"]]
        bottom = 0.0
        for (name, color), v in zip(slices, values):
            height = v * scale
            ax.bar(i, height, bottom=bottom, color=color, width=0.62, edgecolor=SURFACE, linewidth=1)
            if height >= 22:
                ax.text(i, bottom + height / 2, f"{height:.0f}", ha="center", va="center", fontsize=9,
                        color="white" if color in (BLUE, RED, GREEN, GRAY) else INK)
            bottom += height
        ax.text(i, bottom + 4, f"{cycles:.0f} cycles", ha="center", va="bottom", fontsize=10.5, color=INK)
    ax.set_ylim(0, max(count[v]["cycles"] / ops[v] for _, v, _, _ in BOOK) * 1.15)
    style(ax, [label for _, _, label, _ in BOOK], "where each book update's cycles go")
    ax.legend([plt.Rectangle((0, 0), 1, 1, color=c) for _, c in slices], [n for n, _ in slices],
              loc="upper center", bbox_to_anchor=(0.5, -0.09), ncol=2, fontsize=9.5)

    categories = [("order index", VIOLET), ("price levels", MAGENTA), ("shifting & growing arrays", GOLD),
                  ("book logic", LIGHT_BLUE), ("reading the input", GRAY), ("other", PALE)]
    shares = defaultdict(lambda: defaultdict(float))
    for r in read(loads):
        category = r["category"]
        if re.search(r"memmove|memcpy|stl_construct|stl_uninitialized|vector\.tcc", r["source"]):
            category = "shifting & growing arrays"
        elif category == "benchmark harness":
            category = "reading the input"
        elif category not in ("order index", "price levels", "book logic"):
            category = "other"
        shares[r["impl"]][category] += float(r["latency_cycles"])
    ax = axes[1]
    for i, (_, variant, _, _) in enumerate(BOOK):
        total = sum(shares[variant].values()) or 1
        bottom = 0.0
        for name, color in categories:
            v = 100 * shares[variant][name] / total
            ax.bar(i, v, bottom=bottom, color=color, width=0.62, edgecolor=SURFACE, linewidth=1)
            if v >= 6:
                ax.text(i, bottom + v / 2, f"{v:.0f}%", ha="center", va="center", fontsize=9.5,
                        color="white" if color in (VIOLET, GRAY) else INK)
            bottom += v
    ax.set_ylim(0, 100)
    style(ax, [label for _, _, label, _ in BOOK], "slow memory loads, by data structure")
    ax.legend([plt.Rectangle((0, 0), 1, 1, color=c) for _, c in categories], [n for n, _ in categories],
              loc="upper center", bbox_to_anchor=(0.5, -0.09), ncol=2, fontsize=9.5)

    per_op = defaultdict(list)
    for r in read(op_latency):
        per_op[(r["impl"], r["op"])].append(float(r["p50_ns"]))
    operations = [op for op in ("add", "cancel", "modify") if any(k[1] == op for k in per_op)]
    ax = axes[2]
    width = 0.26
    top = 0.0
    for b_i, (_, variant, label, color) in enumerate(BOOK):
        for o_i, op in enumerate(operations):
            v = median(per_op[(variant, op)])
            x = o_i + (b_i - 1) * width
            ax.bar(x, v, width=width * 0.92, color=color)
            ax.text(x, v, f"{v:.0f}", ha="center", va="bottom", fontsize=9.5, color=INK)
            top = max(top, v)
    ax.set_ylim(0, top * 1.18)
    style(ax, operations, "median cost per operation, ns")
    ax.legend([plt.Rectangle((0, 0), 1, 1, color=c) for _, _, _, c in BOOK], [l for _, _, l, _ in BOOK],
              loc="upper center", bbox_to_anchor=(0.5, -0.09), ncol=3, fontsize=9.5)
    fig.tight_layout(w_pad=3)
    save(fig, "04_inner_book_counters.png")


def per_thread_chart(values_of, impls, panels, name, custom=None):
    fig, axes = plt.subplots(2, 3, figsize=(16.5, 8.8))
    width = 0.38
    for ax, (key, title, fmt) in zip(axes.flat, panels):
        if custom and key in custom:
            custom[key](ax, title)
            continue
        top = 0.0
        for i, (impl, _, color) in enumerate(impls):
            sides = ["producer"] if impl == "direct" else ["producer", "consumer"]
            for j, side in enumerate(sides):
                v = values_of(impl, side, key)
                if v != v:
                    continue
                x = i if impl == "direct" else i + (j - 0.5) * width
                ax.bar(x, v, width=width * 0.92, color=color, alpha=1.0 if impl == "direct" or side == "consumer" else 0.55)
                ax.text(x, v, fmt.format(v), ha="center", va="bottom", fontsize=8.5, color=INK)
                top = max(top, v)
        ax.set_ylim(0, top * 1.18 if top > 0 else 1)
        if top >= 10000:
            ax.yaxis.set_major_formatter(matplotlib.ticker.StrMethodFormatter("{x:,.0f}"))
        style(ax, [label for _, label, _ in impls], title)
        ax.tick_params(axis="x", labelsize=9.5)
    fig.legend([plt.Rectangle((0, 0), 1, 1, color=GRAY, alpha=0.55), plt.Rectangle((0, 0), 1, 1, color=GRAY),
                plt.Rectangle((0, 0), 1, 1, color=GREEN)],
               ["producer thread (light)", "consumer thread (dark)", "direct call: one thread does both"],
               loc="upper right", ncol=3, fontsize=10.5)
    fig.tight_layout(rect=(0, 0, 1, 0.95), h_pad=2.5, w_pad=2.5)
    save(fig, name)


def plot_handoff_counters():
    path = DATA / "handoff_counters.csv"
    if not path.exists():
        return
    values = defaultdict(list)
    items = {}
    for r in read(path):
        if r["value"] and r["role"] in ("producer", "consumer"):
            values[(r["impl"], r["role"], r["event"])].append(float(r["value"]))
            items[r["impl"]] = int(r["items"])
    impls = [("direct", "direct\ncall", GREEN), ("mutex", "mutex\nqueue", RED), ("ring_spin", "ring buffer,\nspin", ORANGE),
             ("ring_yield", "ring buffer,\nyield", GOLD), ("ring", "ring buffer,\nback-off", BLUE)]

    def value_of(impl, side, event):
        if event == "context-switches":
            return median(values[(impl, side, event)])
        return median(values[(impl, side, event)]) / items[impl]

    panels = [("cycles", "cycles / item", "{:.0f}"), ("instructions", "instructions / item", "{:.0f}"),
              ("mem_inst_retired.lock_loads", "locked atomic instructions / item", "{:.2f}"),
              ("mem_load_l3_hit_retired.xsnp_hitm", "loads hitting a line modified by the other core / item", "{:.2f}"),
              ("machine_clears.memory_ordering", "pipeline flushes for memory ordering / item", "{:.3f}"),
              ("context-switches", "context switches, whole run", "{:,.0f}")]
    per_thread_chart(value_of, impls, panels, "05_handoff_counters.png")


def plot_combos():
    if not combos_tp or not combos_lat:
        return
    fig, (a, b) = plt.subplots(1, 2, figsize=(15, 5.0))
    groups = [tag for tag, _, _ in HANDOFF]
    books = [(key, label, color) for key, _, label, color in BOOK]
    width = 0.8 / len(books)
    top = 0.0
    for g, handler in enumerate(groups):
        for s, (key, _, color) in enumerate(books):
            runs = tp(handler, key)
            if not runs:
                continue
            x = g + (s - 1) * width
            v = median(runs)
            a.bar(x, v, width=width * 0.92, color=color)
            a.plot([x, x], [min(runs), max(runs)], color=INK, linewidth=0.8)
            a.text(x, max(runs), f"{v:.1f}", ha="center", va="bottom", fontsize=8.5, color=INK)
            top = max(top, max(runs))
    a.set_ylim(0, top * 1.12)
    style(a, [label for _, label, _ in HANDOFF], "throughput, M msgs/s")
    for ax, key, title, floor in ((b, "arrival_p50_ns", "median latency, arrival until in the book (log)", 100),):
        top = 0.0
        for g, handler in enumerate(groups):
            for s, (book, _, color) in enumerate(books):
                v = lat(handler, book, key)
                if v != v:
                    continue
                x = g + (s - 1) * width
                ax.bar(x, v - floor, bottom=floor, width=width * 0.92, color=color)
                ax.text(x, v * 1.1, fmt_ns(v), ha="center", va="bottom", fontsize=8.5, color=INK, rotation=90)
                top = max(top, v)
        log_ns_axis(ax, floor, top * 8)
        style(ax, [label for _, label, _ in HANDOFF], title)
        ax.tick_params(axis="x", labelsize=9.5)
    a.tick_params(axis="x", labelsize=9.5)
    fig.legend([plt.Rectangle((0, 0), 1, 1, color=c) for _, _, c in books], [l for _, l, _ in books],
               loc="lower center", ncol=3, fontsize=11, bbox_to_anchor=(0.5, -0.01))
    fig.tight_layout(rect=(0, 0.07, 1, 1), w_pad=3)
    save(fig, "06_handoff_x_inner_book.png")


BOOK_CODE = r"AggregateSecurityBook|PriceLadder|SortedLevels|MapLevels|BasicFlatOrderIndex|OuterBook|handle_message|locate_level"
HANDOFF_CODE = (r"MessagePipeline|CircularBuffer|MutexPipeline|MutexBuffer|SequencedBuffer|pthread_|__lll_|condition_variable|"
                r"sched_yield|nanosleep|futex|TimedBackoff|Backoff|cpu_pause|std::thread")
DECODE_CODE = r"MboMessage|databento|dbn::|Dbn|MarketClock|_Function_handler"


def function_name(symbol):
    name, depth = [], 0
    for ch in symbol.replace("(anonymous namespace)", "anonymous"):
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth = max(depth - 1, 0)
        elif depth == 0:
            if ch == "(":
                break
            name.append(ch)
    return "".join(name)


def stage_of(symbol, space, handler):
    if space == "k":
        return "os"
    name = function_name(symbol)
    if name.startswith("::"):
        name = symbol
    if re.search(HANDOFF_CODE, name):
        return "handoff"
    if re.search(BOOK_CODE, name):
        return "book"
    if re.search(DECODE_CODE, name):
        return "decode"
    if re.match(r"(void )?std::", name):
        if re.search(BOOK_CODE + r"|_Rb_tree<long", symbol):
            return "book"
        if "deque<mbo::MboMessage" in symbol:
            return "handoff"
    if handler == "mutex-cv" and re.search(r"malloc|free|_int_|cfree", name):
        return "handoff"
    return "other"


def plot_flow():
    path = DATA / "flow_counters.csv"
    if not path.exists():
        return
    values = defaultdict(list)
    records = {}
    for r in read(path):
        if r["value"] and r["role"] in ("producer", "consumer"):
            values[(r["handler"], r["role"], r["event"])].append(float(r["value"]) / float(r["records"]))
            records[r["handler"]] = float(r["records"])

    def value_of(handler, side, key):
        if key == "ipc":
            return median(values[(handler, side, "instructions")]) / median(values[(handler, side, "cycles")])
        if key == "context-switches":
            return 1000 * median(values[(handler, side, key)])
        return median(values[(handler, side, key)])

    stage_order = [("decode", "decode (Databento SDK + parse)", GREEN), ("handoff", "handoff & waiting", RED),
                   ("book", "book update", BLUE), ("os", "OS (kernel)", GRAY), ("other", "other", PALE)]
    stage_share = defaultdict(lambda: defaultdict(float))
    stages_path = DATA / "flow_stages.csv"
    if stages_path.exists():
        producer_cpu = min(r["cpu"] for r in read(stages_path))
        for r in read(stages_path):
            if r["handler"] == "direct" and r["cpu"] != producer_cpu:
                continue
            stage_share[r["handler"]][stage_of(r["symbol"], r["space"], r["handler"])] += float(r["overhead_pct"])

    def total_cycles(handler):
        sides = ("producer",) if handler == "direct" else ("producer", "consumer")
        return sum(value_of(handler, side, "cycles") for side in sides)

    def draw_stages(ax, title):
        top = 0.0
        for i, (handler, _, _) in enumerate(HANDOFF):
            shares = stage_share[handler]
            whole = sum(shares.values())
            if not whole:
                continue
            cycles = total_cycles(handler)
            bottom = 0.0
            for key, _, color in stage_order:
                height = cycles * shares[key] / whole
                ax.bar(i, height, bottom=bottom, width=0.62, color=color, edgecolor=SURFACE, linewidth=1)
                if height >= 70:
                    ax.text(i, bottom + height / 2, f"{height:.0f}", ha="center", va="center", fontsize=8.5,
                            color="white" if color in (GREEN, RED, BLUE, GRAY) else INK)
                bottom += height
            ax.text(i, bottom, f"{cycles:.0f}", ha="center", va="bottom", fontsize=9, color=INK)
            top = max(top, bottom)
        ax.set_ylim(0, top * 1.15 if top else 1)
        style(ax, [label for _, label, _ in HANDOFF], title)
        ax.tick_params(axis="x", labelsize=9.5)
        ax.legend([plt.Rectangle((0, 0), 1, 1, color=c) for _, _, c in stage_order], [n for _, n, _ in stage_order],
                  loc="upper right", fontsize=8.5, ncol=1)

    panels = [("cycles", "CPU cycles / message", "{:.0f}"),
              ("stages", "where each message's cycles go (both threads)", None),
              ("ipc", "instructions per cycle", "{:.2f}"),
              ("mem_load_retired.l3_miss", "loads served from DRAM / message", "{:.2f}"),
              ("mem_load_l3_hit_retired.xsnp_hitm", "cache lines pulled from the other core / message", "{:.2f}"),
              ("context-switches", "context switches / 1,000 messages", "{:.1f}")]
    per_thread_chart(value_of, HANDOFF, panels, "07_whole_flow_counters.png", {"stages": draw_stages})


plot_handoff()
plot_idle()
plot_inner_book()
plot_inner_book_counters()
plot_handoff_counters()
plot_combos()
plot_flow()
