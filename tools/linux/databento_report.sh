#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

usage() {
    cat << EOF
usage: tools/linux/databento_report.sh [options] [file.mbo.dbn.zst ...]

Every Databento-path measurement for the presentation, on real MBO files
(default: testdata/*.mbo.dbn.zst). Writes to \$OUT/databento (default
benchmarks/linux/databento), then draws the charts into its images/ folder.

steps (--only a,b):
  handoff     flat-out msgs/s: parse ceiling, single thread, mutex + condvar queue (batch 1 / 64),
              ring (batch 1 / 64), sharded 2 / 3,
              on the zstd file and a decompressed copy, for a slow and a fast book
  buffers     batch 1 and 64 with each queue: mutex-cv (std::queue + condvar), locked-ring (ring under a
              mutex, polled), ring-yield, ring-spin, ring (back-off), sequenced
  placement   ring batch 64 and single thread with producer and consumer on separate cores,
              SMT siblings, and one logical CPU
  latency     push-to-apply latency per handoff, replay paced at --speed x market time
  books       every book_bench variant on each file (databento_replay variants), plus per-op
              latency from the exported MBO flow
  perf        tools/linux/profile.sh on the exported flows (counters, perf mem, call stacks)

  --runs N            repetitions, alternating order (default 7)
  --speed X           pace for the latency step (default 60)
  --latency-file F    file for the latency step (default: the smallest, so its bursts are not compressed into overload)
  --latency-batches   consumer batch sizes for the latency step (default 64)
  --books a,b         books for the handoff steps (default tree,ladder-cached64)
  --only a,b          subset of steps
  --no-plot           skip the charts
env: PRIMARY_CPU / CROSS_CPU / SMT_CPU, OUT=dir
EOF
}

RUNS=7
SPEED=60
LATENCY_FILE=""
LATENCY_BATCHES=64
HANDOFF_BOOKS=tree,ladder-cached64
ONLY=handoff,buffers,placement,latency,books,perf
PLOT=1
FILES=()
while (($#)); do
    case $1 in
    --runs) RUNS=$2; shift ;;
    --speed) SPEED=$2; shift ;;
    --latency-file) LATENCY_FILE=$2; shift ;;
    --latency-batches) LATENCY_BATCHES=$2; shift ;;
    --books) HANDOFF_BOOKS=$2; shift ;;
    --only) ONLY=$2; shift ;;
    --no-plot) PLOT=0 ;;
    -h | --help) usage; exit 0 ;;
    -*) usage; die "unknown option $1" ;;
    *) FILES+=("$1") ;;
    esac
    shift
done

wants() {
    [[ ,$ONLY, == *",$1,"* ]]
}

if ((${#FILES[@]} == 0)); then
    shopt -s nullglob
    FILES=("$ROOT"/testdata/*.mbo.dbn.zst)
    shopt -u nullglob
fi
((${#FILES[@]} > 0)) || die "no MBO files: copy testdata/*.mbo.dbn.zst from the Mac, or pass paths"

setup_cpus
build_release databento_replay book_bench
[[ -x $RELEASE_BIN/databento_replay ]] || die "databento_replay was not built: run tools/linux/build_databento.sh first"
REPLAY=$RELEASE_BIN/databento_replay
BOOK=$RELEASE_BIN/book_bench
DB_OUT=$OUT/databento
mkdir -p "$DB_OUT/flows" "$DB_OUT/data"
machine_info > "$DB_OUT/env.txt"
warn_noise

IFS=, read -r -a handoff_books <<< "$HANDOFF_BOOKS"
CROSS_CPUS=$(physical_first_cpus "$PRIMARY_CPU")
SMT_CPUS=$PRIMARY_CPU,$SMT_CPU

cpu_ticks() {
    awk -v cpu="cpu$1" '$1 == cpu { total = 0; for (i = 2; i <= NF; i++) total += $i; print total, $5 + $6 }' /proc/stat
}

name_of() {
    basename "$1" | sed 's/\.mbo\.dbn.*//; s/\.dbn.*//' | tr '[:upper:]' '[:lower:]'
}

decompressed_of() {
    local file=$1 out
    out=$DB_OUT/data/$(basename "${file%.zst}")
    if [[ ! -f $out ]]; then
        zstd -q -d -f "$file" -o "$out"
    fi
    echo "$out"
}

msgs_per_sec() {
    grep -oE '\(([0-9]+) msgs/s' | grep -oE '[0-9]+' | head -n 1
}

run_replay() {
    local cpus=$1 file=$2 mode=$3 book=$4 batch=$5 handoff=$6
    LOB_CPUS=$cpus "$REPLAY" "$file" x 0 "" 40 0 "$mode" "$book" "$batch" "$handoff" | msgs_per_sec
}

THROUGHPUT_CSV=$DB_OUT/throughput.csv
if wants handoff || wants buffers || wants placement; then
    echo "file,compression,experiment,placement,mode,book,batch,handoff,run,msgs_per_sec" > "$THROUGHPUT_CSV"
fi

configs=()
if wants handoff; then
    for book in "${handoff_books[@]}"; do
        configs+=("handoff cross_core parse $book 1 ring" "handoff cross_core single $book 1 ring"
            "handoff cross_core mutex $book 1 mutex-cv" "handoff cross_core mutex $book 64 mutex-cv"
            "handoff cross_core ring $book 1 ring" "handoff cross_core ring $book 64 ring"
            "handoff cross_core sharded2 $book 64 ring" "handoff cross_core sharded3 $book 64 ring")
    done
fi
if wants buffers; then
    for book in "${handoff_books[@]}"; do
        for batch in 1 64; do
            for handoff in mutex-cv locked-ring ring-yield ring-spin ring sequenced; do
                configs+=("buffers cross_core ring $book $batch $handoff")
            done
        done
    done
fi
if wants placement; then
    for book in "${handoff_books[@]}"; do
        for placement in cross_core smt one_cpu; do
            [[ $placement == smt && -z $SMT_CPU ]] && continue
            configs+=("placement $placement ring $book 64 ring" "placement $placement single $book 1 ring")
        done
    done
fi

cpus_for() {
    case $1 in
    cross_core) echo "$CROSS_CPUS" ;;
    smt) echo "$SMT_CPUS" ;;
    one_cpu) echo "$PRIMARY_CPU" ;;
    esac
}

if ((${#configs[@]} > 0)); then
    for file in "${FILES[@]}"; do
        name=$(name_of "$file")
        plain=$(decompressed_of "$file")
        for ((run = 0; run < RUNS; run++)); do
            note "$name: throughput run $((run + 1))/$RUNS (${#configs[@]} configurations x 2 encodings)"
            order=("${configs[@]}")
            if ((run % 2 == 1)); then
                mapfile -t order < <(printf '%s\n' "${configs[@]}" | tac)
            fi
            for config in "${order[@]}"; do
                read -r experiment placement mode book batch handoff <<< "$config"
                for compression in zstd none; do
                    [[ $experiment != handoff && $compression == zstd ]] && continue
                    source_file=$([[ $compression == zstd ]] && echo "$file" || echo "$plain")
                    rate=$(run_replay "$(cpus_for "$placement")" "$source_file" "$mode" "$book" "$batch" "$handoff")
                    echo "$name,$compression,$experiment,$placement,$mode,$book,$batch,$handoff,$run,$rate" >> "$THROUGHPUT_CSV"
                done
            done
        done
    done
fi

if wants latency; then
    LATENCY_CSV=$DB_OUT/latency.csv
    rm -f "$LATENCY_CSV" "$LATENCY_CSV.cdf"
    UTIL_CSV=$DB_OUT/consumer_cpu.csv
    echo "file,book,handoff,batch,consumer_busy_pct" > "$UTIL_CSV"
    consumer_cpu=$(echo "$CROSS_CPUS" | cut -d, -f2)
    file=${LATENCY_FILE:-$(ls -Sr "${FILES[@]}" | head -n 1)}
    IFS=, read -r -a latency_batches <<< "$LATENCY_BATCHES"
    name=$(name_of "$file")
    plain=$(decompressed_of "$file")
    for book in "${handoff_books[@]}"; do
            for batch in "${latency_batches[@]}"; do
                for handoff in mutex-cv locked-ring ring-yield ring-spin ring sequenced; do
                    note "$name: latency, $book, $handoff, batch $batch, ${SPEED}x market time"
                    before=$(cpu_ticks "$consumer_cpu")
                    LOB_CPUS=$CROSS_CPUS "$REPLAY" "$plain" x 0 "" 40 "$SPEED" ring "$book" "$batch" "$handoff" \
                        "$LATENCY_CSV" > /dev/null
                    after=$(cpu_ticks "$consumer_cpu")
                    echo "$name,$book,$handoff,$batch,$(awk -v b="$before" -v a="$after" 'BEGIN {
                        split(b, x, " "); split(a, y, " ")
                        printf "%.1f", 100 * ((y[1] - x[1]) - (y[2] - x[2])) / (y[1] - x[1]) }')" >> "$UTIL_CSV"
                done
            done
    done
fi

if wants books; then
    VARIANTS_CSV=$DB_OUT/book_variants.csv
    echo "file,book,levels,index,orders,cache,median_ns,min_ns,resting_orders,depth_hash" > "$VARIANTS_CSV"
    BOOK_LAT_CSV=$DB_OUT/book_latency.csv
    "$BOOK" latency --ops 1 --runs 0 > "$BOOK_LAT_CSV"
    for file in "${FILES[@]}"; do
        name=$(name_of "$file")
        note "$name: every book variant on the real records (15 passes each)"
        taskset -c "$PRIMARY_CPU" "$REPLAY" "$file" x 0 "" 40 0 variants all | tail -n +2 |
            awk -v f="$name" '{ print f "," $0 }' >> "$VARIANTS_CSV"
        note "$name: export as a book_bench flow"
        "$REPLAY" "$file" x 0 "$DB_OUT/flows/$name.flow" 40 0 export > /dev/null
        note "$name: per-operation latency for every book"
        for impl in $("$BOOK" list | cut -d, -f1); do
            taskset -c "$PRIMARY_CPU" "$BOOK" latency --flow "$DB_OUT/flows/$name.flow" --impl "$impl" --runs 1 \
                --scenario "$name" --no-header >> "$BOOK_LAT_CSV"
        done
    done
    "$PYTHON" - "$VARIANTS_CSV" << 'EOF'
import csv, sys
seen = {}
for row in csv.DictReader(open(sys.argv[1])):
    want = seen.setdefault(row["file"], (row["book"], row["depth_hash"]))
    if row["depth_hash"] != want[1]:
        sys.exit(f"MISMATCH {row['file']}: {row['book']} ended in a different book than {want[0]}")
print("every book ended in the same state on every file", file=sys.stderr)
EOF
fi

if wants perf; then
    scenarios=$(for file in "${FILES[@]}"; do name_of "$file"; done | paste -sd,)
    for file in "${FILES[@]}"; do
        name=$(name_of "$file")
        [[ -f $DB_OUT/flows/$name.flow ]] || "$REPLAY" "$file" x 0 "$DB_OUT/flows/$name.flow" 40 0 export > /dev/null
    done
    OUT=$DB_OUT "$ROOT/tools/linux/profile.sh" --scenarios "$scenarios" --only book,mem,cpu \
        --sample-impls tree,hybrid,agg-flat,ladder,pooled-ladder,lazy-handle-ladder --no-plot
fi

if ((PLOT)); then
    "$PYTHON" "$ROOT/tools/plot_databento.py" "$DB_OUT"
fi
note "results in $DB_OUT, charts in $DB_OUT/images"
