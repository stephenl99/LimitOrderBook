#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

usage() {
    cat << EOF
usage: tools/linux/book_bench.sh [options]

Replays the same order flow through every book implementation, one process per
run, pinned to one CPU. Writes to \$OUT/reference (default benchmarks/linux/reference):
  book_throughput.csv  book_latency.csv  book_perf.csv  book_env.txt  book_*.png

  --runs N            timed runs per implementation and scenario (default 5)
  --ops N             timed operations per flow (default 2000000)
  --scenarios a,b     subset of: $(scenario_names | tr '\n' ' ')
  --impls a,b         subset of the implementations (default: all)
  --itch FILE         also replay an ITCH 5.0 file as scenario "itch"
  --perf-scenarios    scenarios to run under perf stat (default equity,deep,cme6)
  --no-latency        skip the per-operation latency pass
  --no-plot           skip the plots
env: PERF=0 skips perf, PRIMARY_CPU=n picks the CPU (default 1), OUT=dir
EOF
}

scenario_args() {
    case $1 in
    equity) echo "--resting 5000" ;;
    thin) echo "--resting 500" ;;
    deep) echo "--resting 50000" ;;
    volatile) echo "--resting 5000 --move 0.05 --near-touch 0.2" ;;
    wide) echo "--resting 5000 --far-fraction 0.05 --far-ticks 5000" ;;
    multi) echo "--resting 2000 --instruments 64" ;;
    cme6) echo "--resting 5000 --instruments 6" ;;
    random_ids) echo "--resting 5000 --random-ids" ;;
    *) die "unknown scenario $1" ;;
    esac
}

scenario_names() {
    printf '%s\n' equity thin deep volatile wide cme6 multi random_ids
}

RUNS=5
OPS=2000000
SCENARIOS=$(scenario_names | paste -sd,)
IMPLS=""
ITCH=""
PERF_SCENARIOS=equity,deep,cme6
LATENCY=1
PLOT=1
while (($#)); do
    case $1 in
    --runs) RUNS=$2; shift ;;
    --ops) OPS=$2; shift ;;
    --scenarios) SCENARIOS=$2; shift ;;
    --impls) IMPLS=$2; shift ;;
    --itch) ITCH=$2; shift ;;
    --perf-scenarios) PERF_SCENARIOS=$2; shift ;;
    --no-latency) LATENCY=0 ;;
    --no-plot) PLOT=0 ;;
    -h | --help) usage; exit 0 ;;
    *) usage; die "unknown option $1" ;;
    esac
    shift
done

setup_cpus
build_release book_bench
BENCH=$RELEASE_BIN/book_bench
DATA_OUT=$OUT/reference
mkdir -p "$OUT/flows" "$DATA_OUT"
machine_info > "$DATA_OUT/book_env.txt"
warn_noise

if [[ -z $IMPLS ]]; then
    IMPLS=$("$BENCH" list | cut -d, -f1 | paste -sd,)
fi
IFS=, read -r -a impl_list <<< "$IMPLS"
IFS=, read -r -a scenario_list <<< "$SCENARIOS"
[[ -n $ITCH ]] && scenario_list+=(itch)

PIN=(taskset -c "$PRIMARY_CPU")
THROUGHPUT_CSV=$DATA_OUT/book_throughput.csv
LATENCY_CSV=$DATA_OUT/book_latency.csv
PERF_CSV=$DATA_OUT/book_perf.csv
"$BENCH" throughput --ops 1 --runs 0 > "$THROUGHPUT_CSV"
"$BENCH" latency --ops 1 --runs 0 > "$LATENCY_CSV"
echo "scenario,impl,ops,event,value,counted_pct" > "$PERF_CSV"

USE_PERF=0
if perf_usable; then
    USE_PERF=1
else
    explain_perf
fi

flow_for() {
    local scenario=$1 file=$OUT/flows/$1.flow
    if [[ $scenario == itch ]]; then
        "$BENCH" generate --itch "$ITCH" --out "$file" > "$OUT/flows/$scenario.txt"
    else
        local args
        read -r -a args <<< "$(scenario_args "$scenario")"
        "$BENCH" generate --ops "$OPS" "${args[@]}" --out "$file" > "$OUT/flows/$scenario.txt"
    fi
    echo "$file"
}

for scenario in "${scenario_list[@]}"; do
    note "scenario $scenario: generating flow"
    flow=$(flow_for "$scenario")
    sed 's/^/   /' "$OUT/flows/$scenario.txt" >&2
    timed_ops=$(grep -o 'timed_ops=[0-9]*' "$OUT/flows/$scenario.txt" | cut -d= -f2)

    for ((run = 0; run < RUNS; run++)); do
        note "scenario $scenario: throughput run $((run + 1))/$RUNS"
        for ((k = 0; k < ${#impl_list[@]}; k++)); do
            impl=${impl_list[$(((run + k) % ${#impl_list[@]}))]}
            "${PIN[@]}" "$BENCH" throughput --flow "$flow" --impl "$impl" --runs 1 --scenario "$scenario" --no-header |
                awk -F, -v OFS=, -v run="$run" '{ $7 = run; print }' >> "$THROUGHPUT_CSV"
        done
    done

    if ((LATENCY)); then
        note "scenario $scenario: per-operation latency"
        for impl in "${impl_list[@]}"; do
            "${PIN[@]}" "$BENCH" latency --flow "$flow" --impl "$impl" --runs 1 --scenario "$scenario" --no-header \
                >> "$LATENCY_CSV"
        done
    fi

    if ((USE_PERF)) && [[ ,$PERF_SCENARIOS, == *",$scenario,"* ]]; then
        note "scenario $scenario: perf stat"
        for impl in "${impl_list[@]}"; do
            raw=$(mktemp)
            perf_stat_gated "$raw" "${PIN[@]}" "$BENCH" throughput --flow "$flow" --impl "$impl" --runs 1 \
                --scenario "$scenario" --no-header > /dev/null
            perf_to_rows "$scenario,$impl,$timed_ops" "$raw" >> "$PERF_CSV"
            rm -f "$raw"
        done
    fi
done

"$PYTHON" - "$THROUGHPUT_CSV" << 'EOF'
import csv, sys
seen = {}
bad = False
for row in csv.DictReader(open(sys.argv[1])):
    want = seen.setdefault(row["scenario"], (row["impl"], row["checksum"]))
    if row["checksum"] != want[1]:
        print(f"MISMATCH {row['scenario']}: {row['impl']} built a different book than {want[0]}", file=sys.stderr)
        bad = True
sys.exit(1 if bad else 0)
EOF
note "every implementation built the same book in every scenario"

if ((PLOT)); then
    "$PYTHON" "$ROOT/tools/plot_linux_bench.py" book "$DATA_OUT"
fi
note "results in $DATA_OUT"
