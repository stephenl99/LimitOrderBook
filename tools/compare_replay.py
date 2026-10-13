#!/usr/bin/env python3
import csv
import re
import statistics
import subprocess
import sys
from pathlib import Path

MODES = ["single", "ring"]


def run_once(binary, data_file, symbol, mode):
    out = subprocess.run([binary, data_file, symbol, "100000", "", "40", "0", mode],
                         capture_output=True, text=True, check=True).stdout
    return int(re.search(r"\((\d+) msgs/s", out).group(1))


def main():
    binary, data_file, symbol, runs, out_path = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), Path(sys.argv[5])
    results = {mode: [] for mode in MODES}
    for run in range(runs):
        order = MODES if run % 2 == 0 else MODES[::-1]
        for mode in order:
            results[mode].append(run_once(binary, data_file, symbol, mode))

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["mode", "run", "msgs_per_sec"])
        for mode in MODES:
            for run, value in enumerate(results[mode]):
                writer.writerow([mode, run, value])

    for mode in MODES:
        values = results[mode]
        quartiles = statistics.quantiles(values, n=4)
        print(f"{mode:7s} median {statistics.median(values) / 1e6:.2f}M msgs/s   "
              f"middle half {quartiles[0] / 1e6:.2f}-{quartiles[2] / 1e6:.2f}M   "
              f"min {min(values) / 1e6:.2f}  max {max(values) / 1e6:.2f}")
    ratio = statistics.median(results["ring"]) / statistics.median(results["single"])
    print(f"ring / single = {ratio:.2f}x   ({runs} alternating runs each, wrote {out_path})")


if __name__ == "__main__":
    main()
