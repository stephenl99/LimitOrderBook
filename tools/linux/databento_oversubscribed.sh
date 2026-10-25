#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

usage() {
    cat << EOF
usage: tools/linux/databento_oversubscribed.sh [options]

Sharded SPSC rings with more threads than cores, so idle consumers compete with
the producer and with each other. Every idle strategy, ladder-cached64 book,
batch 64. Writes oversubscribed_throughput.csv and oversubscribed_latency.csv to
\$OUT/databento/final-demo_data.

  placements:  fits        sharded3 on 4 cores (1 producer + 3 consumers)
               tight       sharded6 on 4 cores (1 producer + 6 consumers)
               oversub     sharded6 on 2 cores (1 producer + 6 consumers)

  --file F       default testdata/cme_mix.mbo.dbn.zst
  --speed X      latency pace (default 60)
  --runs N       throughput repetitions (default 5)
  --latency-runs N  (default 1)
  --tuned S      LOB_BACKOFF for ring-tuned (default 500000,0,5000,1000)
  --only a,b     throughput,latency
EOF
}

FILE=$ROOT/testdata/cme_mix.mbo.dbn.zst
SPEED=60
RUNS=5
LATENCY_RUNS=1
TUNED=500000,0,5000,1000
ONLY=throughput,latency
while (($#)); do
    case $1 in
    --file) FILE=$2; shift ;;
    --speed) SPEED=$2; shift ;;
    --runs) RUNS=$2; shift ;;
    --latency-runs) LATENCY_RUNS=$2; shift ;;
    --tuned) TUNED=$2; shift ;;
    --only) ONLY=$2; shift ;;
    -h | --help) usage; exit 0 ;;
    *) usage; die "unknown option $1" ;;
    esac
    shift
done

setup_cpus
build_release databento_replay
REPLAY=$RELEASE_BIN/databento_replay
DATA=$OUT/databento/final-demo_data
RUN_DIR=$DATA/runs
mkdir -p "$DATA" "$RUN_DIR" "$OUT/databento/data"
BOOK=ladder-cached64
USE_PERF=0
if perf_usable && [[ $(perf_scope) == all ]] && perf stat -e msr/tsc/ -C 0 -- true > /dev/null 2>&1; then
    USE_PERF=1
fi

PLACEMENTS=("fits sharded3 1,2,3,0" "tight sharded6 1,2,3,0" "oversub sharded6 1,2")
STRATEGIES=("spin ring-spin" "yield ring-yield" "back-off ring" "tuned ring-tuned" "mutex-cv mutex-cv")

plain=$OUT/databento/data/$(basename "${FILE%.zst}")
[[ -f $plain ]] || zstd -q -d -f "$FILE" -o "$plain"

perf_columns() {
    local raw=$1
    if ((USE_PERF)) && [[ -f $raw ]]; then
        awk -F, '$3 ~ /ref-cycles/ { r += $1 } $3 ~ /tsc/ { t += $1 } $3 ~ /context-switches/ { s += $1 }
            END { if (t > 0) printf "%.1f,%d", 100 * r / t, s; else printf "," }' "$raw"
    else
        printf ","
    fi
}

run_replay() {
    local raw=$1 cpus=$2 speed=$3 mode=$4 handoff=$5
    shift 5
    if ((USE_PERF)); then
        perf stat -x, -o "$raw" -C "$cpus" -e ref-cycles,msr/tsc/,context-switches -- \
            env LOB_CPUS="$cpus" LOB_BACKOFF="$TUNED" "$REPLAY" "$plain" x 0 "" 40 "$speed" "$mode" "$BOOK" 64 "$handoff" "$@"
    else
        LOB_CPUS=$cpus LOB_BACKOFF=$TUNED "$REPLAY" "$plain" x 0 "" 40 "$speed" "$mode" "$BOOK" 64 "$handoff" "$@"
    fi
}

if [[ ,$ONLY, == *,throughput,* ]]; then
    CSV=$DATA/oversubscribed_throughput.csv
    echo "placement,mode,cpus,strategy,run,msgs_per_sec,cores_busy_pct,context_switches" > "$CSV"
    for ((run = 0; run < RUNS; run++)); do
        for placement in "${PLACEMENTS[@]}"; do
            read -r name mode cpus <<< "$placement"
            for strategy in "${STRATEGIES[@]}"; do
                read -r label handoff <<< "$strategy"
                note "throughput run $((run + 1))/$RUNS: $name, $label"
                raw=$RUN_DIR/oversub_$(date +%s%N).perf
                rate=$(run_replay "$raw" "$cpus" 0 "$mode" "$handoff" | grep -oE '\(([0-9]+) msgs/s' | grep -oE '[0-9]+' | head -n 1)
                echo "$name,$mode,\"$cpus\",$label,$run,$rate,$(perf_columns "$raw")" >> "$CSV"
            done
        done
    done
fi

if [[ ,$ONLY, == *,latency,* ]]; then
    CSV=$DATA/oversubscribed_latency.csv
    for ((run = 0; run < LATENCY_RUNS; run++)); do
        for placement in "${PLACEMENTS[@]}"; do
            read -r name mode cpus <<< "$placement"
            for strategy in "${STRATEGIES[@]}"; do
                read -r label handoff <<< "$strategy"
                note "latency run $((run + 1))/$LATENCY_RUNS: $name, $label, ${SPEED}x"
                id=$(date +%s%N)
                raw=$RUN_DIR/oversub_$id.perf
                latency=$RUN_DIR/oversub_$id.csv
                run_replay "$raw" "$cpus" "$SPEED" "$mode" "$handoff" "$latency" > /dev/null
                "$PYTHON" - "$latency" "$CSV" "$name" "$mode" "$cpus" "$label" "$(perf_columns "$raw")" << 'EOF'
import csv, os, sys
latency, out, name, mode, cpus, label, perf = sys.argv[1:]
row = list(csv.DictReader(open(latency)))[-1]
busy, switches = (perf.split(",") + ["", ""])[:2]
fresh = not os.path.exists(out)
with open(out, "a", newline="") as f:
    writer = csv.writer(f)
    if fresh:
        writer.writerow(["placement", "mode", "cpus", "strategy", "cores_busy_pct", "context_switches", "count", "p50_ns",
                         "p90_ns", "p99_ns", "p999_ns", "max_ns"])
    writer.writerow([name, mode, cpus, label, busy, switches, row["count"], row["p50_ns"], row["p90_ns"], row["p99_ns"],
                     row["p999_ns"], row["max_ns"]])
EOF
            done
        done
    done
fi
note "results in $DATA"
