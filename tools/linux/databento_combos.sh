#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

usage() {
    cat << EOF
usage: tools/linux/databento_combos.sh [options]

Every handler x inner book combination on the Databento path. Writes to
\$OUT/databento (default benchmarks/linux/databento):
  combos_throughput.csv  flat-out msgs/s on the decompressed --throughput-file
  combos_latency.csv     push-to-pickup latency, paced at --speed x on --latency-file

  --runs N              throughput repetitions, alternating order (default 5)
  --speed X             latency pace (default 60)
  --throughput-file F   default testdata/cme_mix.mbo.dbn.zst
  --latency-file F      default testdata/ESZ6.mbo.dbn.zst
  --only a,b            throughput,latency (default both)
EOF
}

RUNS=5
SPEED=60
THROUGHPUT_FILE=$ROOT/testdata/cme_mix.mbo.dbn.zst
LATENCY_FILE=$ROOT/testdata/ESZ6.mbo.dbn.zst
ONLY=throughput,latency
while (($#)); do
    case $1 in
    --runs) RUNS=$2; shift ;;
    --speed) SPEED=$2; shift ;;
    --throughput-file) THROUGHPUT_FILE=$2; shift ;;
    --latency-file) LATENCY_FILE=$2; shift ;;
    --only) ONLY=$2; shift ;;
    -h | --help) usage; exit 0 ;;
    *) usage; die "unknown option $1" ;;
    esac
    shift
done

setup_cpus
build_release databento_replay
REPLAY=$RELEASE_BIN/databento_replay
DB_OUT=$OUT/databento
DATA_OUT=$DB_OUT/reference
mkdir -p "$DB_OUT/data" "$DATA_OUT"
warn_noise
CPUS=$(physical_first_cpus "$PRIMARY_CPU")

BOOKS=(tree-cached64 hybrid-cached64 agg-flat-cached64 ladder-cached64 pooled-hybrid pooled-ladder lazy-handle-ladder)
HANDLERS=("single 1 ring" "mutex 64 mutex-cv" "ring 64 locked-ring" "ring 64 ring-yield" "ring 64 ring-spin"
    "ring 64 ring" "ring 64 sequenced" "sharded2 64 ring" "sharded3 64 ring")

decompressed() {
    local out=$DB_OUT/data/$(basename "${1%.zst}")
    [[ -f $out ]] || zstd -q -d -f "$1" -o "$out"
    echo "$out"
}

handler_name() {
    case "$1 $3" in
    "single ring") echo single ;;
    "mutex mutex-cv") echo mutex-cv ;;
    "sharded2 ring") echo sharded2 ;;
    "sharded3 ring") echo sharded3 ;;
    *) echo "$3" ;;
    esac
}

if [[ ,$ONLY, == *,throughput,* ]]; then
    file=$(decompressed "$THROUGHPUT_FILE")
    CSV=$DATA_OUT/combos_throughput.csv
    echo "file,book,handler,run,msgs_per_sec" > "$CSV"
    combos=()
    for book in "${BOOKS[@]}"; do
        for handler in "${HANDLERS[@]}"; do
            combos+=("$book $handler")
        done
    done
    for ((run = 0; run < RUNS; run++)); do
        note "throughput run $((run + 1))/$RUNS (${#combos[@]} combinations)"
        order=("${combos[@]}")
        if ((run % 2 == 1)); then
            mapfile -t order < <(printf '%s\n' "${combos[@]}" | tac)
        fi
        for combo in "${order[@]}"; do
            read -r book mode batch handoff <<< "$combo"
            rate=$(LOB_CPUS=$CPUS "$REPLAY" "$file" x 0 "" 40 0 "$mode" "$book" "$batch" "$handoff" |
                grep -oE '\(([0-9]+) msgs/s' | grep -oE '[0-9]+' | head -n 1)
            echo "$(basename "$file"),$book,$(handler_name "$mode" "$batch" "$handoff"),$run,$rate" >> "$CSV"
        done
    done
fi

if [[ ,$ONLY, == *,latency,* ]]; then
    file=$(decompressed "$LATENCY_FILE")
    CSV=$DATA_OUT/combos_latency.csv
    rm -f "$CSV" "$CSV.cdf"
    for book in "${BOOKS[@]}"; do
        for handoff in mutex-cv locked-ring ring-yield ring-spin ring sequenced; do
            note "latency: $book, $handoff, ${SPEED}x market time"
            LOB_CPUS=$CPUS "$REPLAY" "$file" x 0 "" 40 "$SPEED" ring "$book" 64 "$handoff" "$CSV" > /dev/null
        done
    done
fi
note "results in $DATA_OUT"
