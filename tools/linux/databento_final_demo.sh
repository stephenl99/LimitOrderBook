#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

usage() {
    cat << EOF
usage: tools/linux/databento_final_demo.sh [options]

Data for the final demo charts, ladder-cached64 book, batch 64. Writes to
\$OUT/databento/final-demo_data (default benchmarks/linux/databento/final-demo_data).

steps (--only a,b):
  sweep       idle-strategy tuning: ring-tuned over spin x sleep (no yield stage), plus the fixed
              strategies, on --sweep-file paced at --sweep-speed, each with no neighbour, a nice 0
              neighbour and a nice 19 neighbour busy-looping on every consumer core
  throughput  flat-out msgs/s on --file for every handler, with and without a nice 0 neighbour
  latency     push-to-apply latency on --file paced at --speed, every handler including sharded,
              consumer CPU from perf (unhalted reference cycles / TSC) and /proc/stat, plus the
              idle strategies again under both neighbours

  --file F           default testdata/cme_mix.mbo.dbn.zst
  --speed X          default 60
  --sweep-file F     default testdata/ESZ6.mbo.dbn.zst
  --sweep-speed X    default 240
  --runs N           repetitions for throughput (default 7) and latency (default 3, --latency-runs)
  --tuned S          LOB_BACKOFF for ring-tuned in the throughput and latency steps
                     (spin_ns,yield_ns,sleep_ns,timer_slack_ns; default 500000,0,5000,1000)
EOF
}

FILE=$ROOT/testdata/cme_mix.mbo.dbn.zst
SPEED=60
SWEEP_FILE=$ROOT/testdata/ESZ6.mbo.dbn.zst
SWEEP_SPEED=240
RUNS=7
LATENCY_RUNS=3
SWEEP_RUNS=2
TUNED=500000,0,5000,1000
ONLY=sweep,throughput,latency
while (($#)); do
    case $1 in
    --file) FILE=$2; shift ;;
    --speed) SPEED=$2; shift ;;
    --sweep-file) SWEEP_FILE=$2; shift ;;
    --sweep-speed) SWEEP_SPEED=$2; shift ;;
    --runs) RUNS=$2; shift ;;
    --latency-runs) LATENCY_RUNS=$2; shift ;;
    --sweep-runs) SWEEP_RUNS=$2; shift ;;
    --tuned) TUNED=$2; shift ;;
    --only) ONLY=$2; shift ;;
    -h | --help) usage; exit 0 ;;
    *) usage; die "unknown option $1" ;;
    esac
    shift
done

wants() {
    [[ ,$ONLY, == *",$1,"* ]]
}

setup_cpus
build_release databento_replay
REPLAY=$RELEASE_BIN/databento_replay
DATA=$OUT/databento/final-demo_data
RUN_DIR=$DATA/runs
mkdir -p "$DATA" "$RUN_DIR" "$OUT/databento/data"
machine_info > "$DATA/env.txt"
warn_noise
BOOK=ladder-cached64
CPUS=1,2,3,0
TSC_HZ=$(journalctl -k -b 2> /dev/null | grep -oE 'tsc: Detected [0-9.]+ MHz TSC' | grep -oE '[0-9.]+' | head -n 1 || true)
USE_PERF=0
if perf_usable && [[ $(perf_scope) == all ]] && perf stat -e msr/tsc/ -C 0 -- true > /dev/null 2>&1; then
    USE_PERF=1
else
    note "perf counters unavailable (sudo sysctl kernel.perf_event_paranoid=0); consumer CPU from /proc/stat only"
fi

decompressed() {
    local out=$OUT/databento/data/$(basename "${1%.zst}")
    [[ -f $out ]] || zstd -q -d -f "$1" -o "$out"
    echo "$out"
}

consumer_cpus() {
    local mode=$1 count=1
    [[ $mode == sharded* ]] && count=${mode#sharded}
    echo "$CPUS" | cut -d, -f2-$((count + 1))
}

cpu_ticks() {
    awk -v list=",$1," '$1 ~ /^cpu[0-9]+$/ && index(list, "," substr($1, 4) ",") {
        for (i = 2; i <= NF; i++) total += $i; idle += $5 + $6 } END { print total, idle }' /proc/stat
}

process_ticks() {
    local total=0 pid
    for pid in "$@"; do
        total=$((total + $(awk '{ print $14 + $15 }' "/proc/$pid/stat")))
    done
    echo "$total"
}

start_neighbours() {
    local cpus=$1 nice_level=$2 cpu
    NEIGHBOURS=()
    for cpu in ${cpus//,/ }; do
        taskset -c "$cpu" nice -n "$nice_level" bash -c 'while :; do :; done' &
        NEIGHBOURS+=($!)
    done
    sleep 0.2
}

stop_neighbours() {
    local pid
    for pid in "${NEIGHBOURS[@]}"; do
        kill "$pid" 2> /dev/null || true
        wait "$pid" 2> /dev/null || true
    done
    NEIGHBOURS=()
}

NEIGHBOURS=()
trap stop_neighbours EXIT

measure() {
    local csv=$1 tag=$2 scenario=$3 file=$4 speed=$5 mode=$6 handoff=$7 backoff=$8
    local cpus run_id latency raw before after hog_before hog_after started finished
    cpus=$(consumer_cpus "$mode")
    run_id=$(date +%s%N)
    latency=$RUN_DIR/$run_id.csv
    raw=$RUN_DIR/$run_id.perf
    if [[ $scenario != none ]]; then
        start_neighbours "$cpus" "${scenario#nice}"
        hog_before=$(process_ticks "${NEIGHBOURS[@]}")
    fi
    before=$(cpu_ticks "$cpus")
    started=$(date +%s.%N)
    if ((USE_PERF)); then
        perf stat -x, -o "$raw" -C "$cpus" -e ref-cycles,msr/tsc/,context-switches -- \
            env LOB_CPUS="$CPUS" LOB_BACKOFF="$backoff" "$REPLAY" "$file" x 0 "" 40 "$speed" "$mode" "$BOOK" 64 \
            "$handoff" "$latency" > /dev/null
    else
        LOB_CPUS=$CPUS LOB_BACKOFF=$backoff "$REPLAY" "$file" x 0 "" 40 "$speed" "$mode" "$BOOK" 64 "$handoff" \
            "$latency" > /dev/null
    fi
    finished=$(date +%s.%N)
    after=$(cpu_ticks "$cpus")
    local neighbour_pct=""
    if [[ $scenario != none ]]; then
        hog_after=$(process_ticks "${NEIGHBOURS[@]}")
        stop_neighbours
        neighbour_pct=$(awk -v a="$hog_before" -v b="$hog_after" -v s="$started" -v e="$finished" -v n="$(echo "$cpus" | tr ',' '\n' | wc -l)" \
            'BEGIN { printf "%.1f", 100 * (b - a) / 100 / (e - s) / n }')
    fi
    local proc_busy perf_busy="" switches=""
    proc_busy=$(awk -v b="$before" -v a="$after" 'BEGIN { split(b, x, " "); split(a, y, " ");
        printf "%.1f", 100 * ((y[1] - x[1]) - (y[2] - x[2])) / (y[1] - x[1]) }')
    if ((USE_PERF)) && [[ -f $raw ]]; then
        perf_busy=$(awk -F, '$3 ~ /ref-cycles/ { r += $1 } $3 ~ /tsc/ { t += $1 } END { if (t > 0) printf "%.1f", 100 * r / t }' "$raw")
        switches=$(awk -F, '$3 ~ /context-switches/ { s += $1 } END { print s + 0 }' "$raw")
    fi
    "$PYTHON" - "$latency" "$csv" "$tag" "$scenario" "$mode" "$handoff" "$backoff" "$proc_busy" "$perf_busy" \
        "$switches" "$neighbour_pct" << 'EOF'
import csv, os, sys
latency, out, *fields = sys.argv[1:]
row = list(csv.DictReader(open(latency)))[-1]
fresh = not os.path.exists(out)
with open(out, "a", newline="") as f:
    writer = csv.writer(f)
    if fresh:
        writer.writerow(["tag", "scenario", "mode", "handoff", "backoff", "consumer_busy_proc_pct", "consumer_busy_perf_pct",
                         "context_switches", "neighbour_cpu_pct", "file", "speed", "count", "p50_ns", "p90_ns", "p99_ns",
                         "p999_ns", "p9999_ns", "max_ns", "run_file"])
    writer.writerow(fields + [row["file"], row["speed"], row["count"], row["p50_ns"], row["p90_ns"], row["p99_ns"],
                              row["p999_ns"], row["p9999_ns"], row["max_ns"], os.path.basename(latency)])
EOF
}

if wants sweep; then
    file=$(decompressed "$SWEEP_FILE")
    SWEEP_CSV=$DATA/idle_sweep.csv
    configs=("spin ring-spin -" "yield ring-yield -" "balanced ring -")
    for spin in 0 20000 100000 500000; do
        for sleep_ns in 5000 20000 50000; do
            configs+=("timed-${spin}-0-${sleep_ns}-1000 ring-tuned $spin,0,$sleep_ns,1000")
        done
    done
    configs+=("timed-20000-0-20000-default ring-tuned 20000,0,20000,0"
        "timed-20000-200000-20000-1000 ring-tuned 20000,200000,20000,1000")
    for ((run = 0; run < SWEEP_RUNS; run++)); do
        for scenario in none nice0 nice19; do
            for config in "${configs[@]}"; do
                read -r tag handoff backoff <<< "$config"
                note "sweep run $((run + 1))/$SWEEP_RUNS, $scenario: $tag"
                measure "$SWEEP_CSV" "$tag" "$scenario" "$file" "$SWEEP_SPEED" ring "$handoff" "$backoff"
            done
        done
    done
fi

HANDLERS=("direct single ring" "mutex-cv mutex mutex-cv" "locked-ring ring locked-ring" "ring ring ring"
    "sharded2 sharded2 ring" "sharded3 sharded3 ring" "spin ring ring-spin" "yield ring ring-yield"
    "tuned ring ring-tuned")

if wants throughput; then
    file=$(decompressed "$FILE")
    THROUGHPUT_CSV=$DATA/throughput.csv
    echo "tag,scenario,mode,handoff,run,msgs_per_sec" > "$THROUGHPUT_CSV"
    for ((run = 0; run < RUNS; run++)); do
        note "throughput run $((run + 1))/$RUNS"
        for scenario in none nice0; do
            for handler in "${HANDLERS[@]}"; do
                read -r tag mode handoff <<< "$handler"
                [[ $scenario != none && $tag != spin && $tag != yield && $tag != ring && $tag != tuned ]] && continue
                if [[ $scenario != none ]]; then
                    start_neighbours "$(consumer_cpus "$mode")" 0
                fi
                rate=$(LOB_CPUS=$CPUS LOB_BACKOFF=$TUNED "$REPLAY" "$file" x 0 "" 40 0 "$mode" "$BOOK" 64 "$handoff" |
                    grep -oE '\(([0-9]+) msgs/s' | grep -oE '[0-9]+' | head -n 1)
                [[ $scenario != none ]] && stop_neighbours
                echo "$tag,$scenario,$mode,$handoff,$run,$rate" >> "$THROUGHPUT_CSV"
            done
        done
    done
fi

if wants latency; then
    file=$(decompressed "$FILE")
    LATENCY_CSV=$DATA/latency.csv
    for ((run = 0; run < LATENCY_RUNS; run++)); do
        for handler in "${HANDLERS[@]}"; do
            read -r tag mode handoff <<< "$handler"
            [[ $tag == direct ]] && continue
            note "latency run $((run + 1))/$LATENCY_RUNS: $tag, ${SPEED}x"
            measure "$LATENCY_CSV" "$tag" none "$file" "$SPEED" "$mode" "$handoff" "$TUNED"
        done
        for scenario in nice0 nice19; do
            for handler in "spin ring ring-spin" "yield ring ring-yield" "ring ring ring" "tuned ring ring-tuned"; do
                read -r tag mode handoff <<< "$handler"
                note "latency run $((run + 1))/$LATENCY_RUNS: $tag with a $scenario neighbour, ${SPEED}x"
                measure "$LATENCY_CSV" "$tag" "$scenario" "$file" "$SPEED" "$mode" "$handoff" "$TUNED"
            done
        done
    done
fi
note "results in $DATA"
