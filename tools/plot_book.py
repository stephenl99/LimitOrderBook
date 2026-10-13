#!/usr/bin/env python3
import csv
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt
import numpy as np

BID = "#2a78d6"
ASK = "#eb6834"
INK = "#0b0b0b"
MUTED = "#52514e"
GRID = "#e4e3de"
TICK = 0.25


def load(path):
    snapshots = defaultdict(lambda: {"bid": [], "ask": []})
    with open(path) as f:
        for row in csv.DictReader(f):
            snapshots[int(row["ts_ns"])][row["side"]].append((float(row["price"]), int(row["size"])))
    times = sorted(snapshots)
    return times, [snapshots[t] for t in times]


def as_datetime(ts_ns):
    return datetime.fromtimestamp(ts_ns / 1e9, tz=timezone.utc)


def style(ax):
    ax.spines[["top", "right"]].set_visible(False)
    ax.spines[["left", "bottom"]].set_color(MUTED)
    ax.tick_params(colors=MUTED, labelsize=9)
    ax.yaxis.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def time_axis(ax):
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M", tz=timezone.utc))
    ax.set_xlabel("time (UTC)", color=MUTED, fontsize=9)


def top_of_book(times, books, symbol, day, out):
    stamps = [as_datetime(t) for t in times]
    bids = [b["bid"][0][0] for b in books]
    asks = [b["ask"][0][0] for b in books]

    fig, ax = plt.subplots(figsize=(8, 4))
    ax.step(stamps, asks, where="post", color=ASK, linewidth=1.2, label="best ask")
    ax.step(stamps, bids, where="post", color=BID, linewidth=1.2, label="best bid")
    ax.set_ylabel("price", color=MUTED, fontsize=9)
    ax.set_title(f"{symbol} best bid and ask, {day}", loc="left", fontsize=11, color=INK)
    ax.legend(frameon=False, fontsize=9, loc="upper right")
    time_axis(ax)
    style(ax)
    fig.tight_layout()
    fig.savefig(out / "book_top.png", dpi=160)
    plt.close(fig)


def depth_chart(times, books, symbol, out):
    book = books[-1]
    stamp = as_datetime(times[-1]).strftime("%Y-%m-%d %H:%M:%S UTC")

    fig, ax = plt.subplots(figsize=(8, 4))
    for side, color, name in (("bid", BID, "bids"), ("ask", ASK, "asks")):
        prices = [p for p, _ in book[side]]
        cumulative = np.cumsum([s for _, s in book[side]])
        ax.fill_between(prices, cumulative, step="mid", color=color, alpha=0.18, linewidth=0)
        ax.step(prices, cumulative, where="mid", color=color, linewidth=1.8, label=name)

    best_bid, best_ask = book["bid"][0][0], book["ask"][0][0]
    ax.axvline((best_bid + best_ask) / 2, color=MUTED, linewidth=0.8, linestyle=":")
    ax.set_ylim(bottom=0)
    ax.set_xlabel("price", color=MUTED, fontsize=9)
    ax.set_ylabel("cumulative contracts resting", color=MUTED, fontsize=9)
    ax.set_title(f"{symbol} depth at {stamp}", loc="left", fontsize=11, color=INK)
    ax.legend(frameon=False, fontsize=9, loc="upper center")
    style(ax)
    fig.tight_layout()
    fig.savefig(out / "book_depth.png", dpi=160)
    plt.close(fig)


def heatmap(times, books, symbol, day, out):
    low = min(p for b in books for side in ("bid", "ask") for p, _ in b[side])
    high = max(p for b in books for side in ("bid", "ask") for p, _ in b[side])
    rows = int(round((high - low) / TICK)) + 1
    grid = np.full((rows, len(times)), np.nan)
    for column, book in enumerate(books):
        for side in ("bid", "ask"):
            for price, size in book[side]:
                grid[int(round((price - low) / TICK)), column] = size

    start, end = mdates.date2num(as_datetime(times[0])), mdates.date2num(as_datetime(times[-1]))
    mids = [(b["bid"][0][0] + b["ask"][0][0]) / 2 for b in books]

    fig, ax = plt.subplots(figsize=(9, 4.6))
    image = ax.imshow(grid, aspect="auto", origin="lower", cmap="Blues", interpolation="nearest",
                      extent=(start, end, low - TICK / 2, high + TICK / 2),
                      vmin=0, vmax=np.nanpercentile(grid, 99))
    ax.plot([mdates.date2num(as_datetime(t)) for t in times], mids, color=INK, linewidth=0.8, label="mid price")
    ax.xaxis_date(tz=timezone.utc)
    time_axis(ax)
    ax.set_ylabel("price", color=MUTED, fontsize=9)
    ax.set_title(f"{symbol} resting size by price level, {day}", loc="left", fontsize=11, color=INK)
    ax.legend(frameon=False, fontsize=9, loc="upper right")
    bar = fig.colorbar(image, ax=ax, pad=0.02)
    bar.set_label("contracts at level", color=MUTED, fontsize=9)
    bar.ax.tick_params(colors=MUTED, labelsize=8)
    bar.outline.set_visible(False)
    ax.spines[["top", "right"]].set_visible(False)
    ax.spines[["left", "bottom"]].set_color(MUTED)
    ax.tick_params(colors=MUTED, labelsize=9)
    fig.tight_layout()
    fig.savefig(out / "book_heatmap.png", dpi=160)
    plt.close(fig)


def main():
    times, books = load(sys.argv[1])
    books_with_both = [(t, b) for t, b in zip(times, books) if b["bid"] and b["ask"]]
    times = [t for t, _ in books_with_both]
    books = [b for _, b in books_with_both]
    symbol = sys.argv[2]
    out = Path(sys.argv[3])
    out.mkdir(parents=True, exist_ok=True)
    day = as_datetime(times[0]).strftime("%Y-%m-%d")

    top_of_book(times, books, symbol, day, out)
    depth_chart(times, books, symbol, out)
    heatmap(times, books, symbol, day, out)
    print(f"wrote {out}/book_top.png, book_depth.png, book_heatmap.png from {len(times)} snapshots")


if __name__ == "__main__":
    main()
