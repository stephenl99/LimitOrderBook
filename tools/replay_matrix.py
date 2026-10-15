#!/usr/bin/env python3
import argparse
import csv
import re
import statistics
import subprocess
from pathlib import Path

DEFAULT_BOOKS = ["tree", "hybrid", "agg-flat", "ladder-cached", "ladder-cached64"]
DEFAULT_MODES = ["single/1", "ring/1", "ring/64", "sharded2/64"]


def run_once(binary, data_file, mode, book, batch):
    out = subprocess.run([binary, data_file, "matrix", "0", "", "40", "0", mode, book, batch],
                         capture_output=True, text=True, check=True).stdout
    return int(re.search(r"\((\d+) msgs/s", out).group(1)) / 1e6


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("out")
    parser.add_argument("runs", type=int)
    parser.add_argument("files", nargs="+")
    parser.add_argument("--books", default=",".join(DEFAULT_BOOKS))
    parser.add_argument("--modes", default=",".join(DEFAULT_MODES))
    args = parser.parse_args()

    configs = [("parse", "tree", "1")]
    for mode in args.modes.split(","):
        name, batch = mode.split("/")
        configs += [(name, book, batch) for book in args.books.split(",")]

    results = {}
    for run in range(args.runs):
        order = configs if run % 2 == 0 else configs[::-1]
        for data_file in args.files:
            for config in order:
                results.setdefault((data_file, config), []).append(run_once(args.binary, data_file, *config))

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["file", "mode", "book", "batch", "run", "msgs_per_sec_m"])
        for (data_file, config), values in results.items():
            for run, value in enumerate(values):
                writer.writerow([Path(data_file).name, *config, run, f"{value:.3f}"])

    for data_file in args.files:
        print(Path(data_file).name)
        for config in configs:
            values = results[(data_file, config)]
            quartiles = statistics.quantiles(values) if len(values) > 1 else [values[0]] * 3
            print(f"  {config[0]:9s} {config[1]:16s} batch {config[2]:>2s}  {statistics.median(values):6.2f} M/s"
                  f"  ({quartiles[0]:.2f}-{quartiles[2]:.2f})")


if __name__ == "__main__":
    main()
