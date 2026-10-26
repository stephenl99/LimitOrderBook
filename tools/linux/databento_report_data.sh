#!/usr/bin/env bash
set -euo pipefail
SELF=$(realpath "${BASH_SOURCE[0]}")
source "$(dirname "$SELF")/common.sh"

usage() {
    cat << EOF
usage: tools/linux/databento_report_data.sh [options]

All data behind benchmarks/linux/databento/final-demo_images/report, collected in one session with perf.
Writes CSVs to \$OUT/databento/final-demo_data (default benchmarks/linux/databento/final-demo_data);
REPORT.md in that folder describes every file and column.

steps (--only a,b; default all, in this order):
  queue              handoff counters: queue_bench one <impl>, integer items, per CPU
  book               inner books on the real records: book-only timing, per-op latency, top-down metrics,
                     slow loads by data structure
  flow               whole-flow counters: databento_replay flat out, counted only inside the replay loop, per CPU
  combos_throughput  every handoff x inner book, flat out
  idle_throughput    every idle strategy, 1 / 2 / 4 order books sharing the same 2 cores, flat out
  combos_latency     every handoff x inner book, paced at --speed x market time
  idle_latency       every idle strategy, 1 / 2 / 4 order books sharing the same 2 cores, paced

  --file F           default testdata/cme_mix.mbo.dbn.zst (decompressed once into databento/data/)
  --speed X          default 60
  --runs N           flat-out repetitions (default 5)
  --latency-runs N   paced repetitions (default 2)
  --only a,b
env: OUT=dir; PRIMARY_CPU / CROSS_CPU pick the two cores (default 1 and 2)
EOF
}

REPLAY=$RELEASE_BIN/databento_replay

launch_books() {
    local count=$1 handoff=$2 speed=$3 prefix=$4 file=$5 k cpus
    local pids=()
    for ((k = 0; k < count; k++)); do
        if ((k % 2 == 0)); then cpus=$PRIMARY_CPU,$CROSS_CPU; else cpus=$CROSS_CPU,$PRIMARY_CPU; fi
        if [[ $speed == 0 ]]; then
            LOB_CPUS=$cpus "$REPLAY" "$file" x 0 "" 40 0 ring ladder-cached64 64 "$handoff" > "${prefix}_book$k.out" &
        else
            LOB_CPUS=$cpus "$REPLAY" "$file" x 0 "" 40 "$speed" ring ladder-cached64 64 "$handoff" "${prefix}_book$k.csv" \
                > "${prefix}_book$k.out" &
        fi
        pids+=($!)
    done
    wait "${pids[@]}"
}

if [[ ${1:-} == --launch-books ]]; then
    setup_cpus
    launch_books "$2" "$3" "$4" "$5" "$6"
    exit 0
fi

FILE=$ROOT/testdata/cme_mix.mbo.dbn.zst
SPEED=60
RUNS=5
LATENCY_RUNS=2
ONLY=queue,book,flow,combos_throughput,idle_throughput,combos_latency,idle_latency
while (($#)); do
    case $1 in
    --file) FILE=$2; shift ;;
    --speed) SPEED=$2; shift ;;
    --runs) RUNS=$2; shift ;;
    --latency-runs) LATENCY_RUNS=$2; shift ;;
    --only) ONLY=$2; shift ;;
    -h | --help) usage; exit 0 ;;
    *) usage; die "unknown option $1" ;;
    esac
    shift
done

wants() {
    [[ ,$ONLY, == *",$1,"* ]]
}

on_ac() {
    local online
    online=$(cat /sys/class/power_supply/AC*/online 2> /dev/null || echo 1)
    [[ $online != 0 ]]
}

wait_for_ac() {
    on_ac && return
    note "on battery: paused until the charger is back, so every run sees the same power settings"
    until on_ac; do
        sleep 10
    done
}

[[ $(perf_scope) == all ]] || die "needs perf counters: sudo sysctl kernel.perf_event_paranoid=0"
setup_cpus
build_release databento_replay book_bench queue_bench
DATA=$OUT/databento/final-demo_data
RUN_DIR=$DATA/runs
FLOWS=$OUT/databento/flows
mkdir -p "$DATA" "$RUN_DIR" "$FLOWS" "$OUT/databento/data"
machine_info > "$DATA/env.txt"
echo "nmi_watchdog: $(cat /proc/sys/kernel/nmi_watchdog)" >> "$DATA/env.txt"
echo "battery_or_ac: $(cat /sys/class/power_supply/AC*/online 2> /dev/null | sed 's/1/ac/; s/0/battery/')" >> "$DATA/env.txt"
echo "energy_performance_preference: $(cat /sys/devices/system/cpu/cpu$PRIMARY_CPU/cpufreq/energy_performance_preference 2> /dev/null)" >> "$DATA/env.txt"
echo "steps: $ONLY" >> "$DATA/env.txt"
warn_noise
PAIR=$PRIMARY_CPU,$CROSS_CPU

plain=$OUT/databento/data/$(basename "${FILE%.zst}")
[[ -f $plain ]] || zstd -q -d -f "$FILE" -o "$plain"
NAME=$(basename "$plain" | sed 's/\..*//')

HANDLERS=("direct single ring" "mutex-cv mutex mutex-cv" "spin ring ring-spin" "yield ring ring-yield"
    "back-off ring ring")
BOOKS=("map agg-map-cached64" "vector agg-flat-cached64" "ladder ladder-cached64")
STRATEGIES=("spin ring-spin" "yield ring-yield" "back-off ring" "tuned ring-tuned")

gated() {
    local out=$1 stdout=$2
    shift 2
    local dir ctl ack status
    dir=$(mktemp -d)
    mkfifo "$dir/ctl" "$dir/ack"
    exec {ctl}<> "$dir/ctl"
    exec {ack}<> "$dir/ack"
    LOB_PERF_CTL_FD=$ctl LOB_PERF_ACK_FD=$ack perf stat -x, -o "$out" -D -1 --control "fd:$ctl,$ack" "$@" > "$stdout"
    status=$?
    exec {ctl}>&-
    exec {ack}>&-
    return $status
}

replay_fields() {
    "$PYTHON" -c '
import re, sys
text = open(sys.argv[1]).read()
m = re.search(r"replayed (\d+) records in ([0-9.e+-]+)s(?: \((\d+) msgs/s)?", text)
print(",".join([m.group(1), m.group(2), m.group(3) or ""]) if m else ",,")' "$1"
}

per_cpu_rows() {
    awk -F, -v OFS=, -v prefix="$1" -v producer="CPU$PRIMARY_CPU" -v consumer="CPU$CROSS_CPU" '
        /^CPU/ { role = $1 == producer ? "producer" : ($1 == consumer ? "consumer" : "other")
                 event = $4; gsub(/^cpu\/|\/$|:u$/, "", event)
                 value = $2 ~ /^</ ? "" : $2
                 print prefix, $1, role, event, value, $6 }' "$2"
}

if wants queue; then
    wait_for_ac
    note "queue: handoff counters on integer items, per CPU"
    CSV=$DATA/handoff_counters.csv
    echo "impl,run,items,cpu,role,event,value,counted_pct" > "$CSV"
    for ((run = 0; run < 3; run++)); do
        for impl in direct mutex ring_spin ring_yield ring; do
            raw=$RUN_DIR/queue_${impl}_$run.perf
            LOB_CPUS=$PAIR perf stat -x, -o "$raw" -C "$PAIR" -A \
                -e cycles,instructions,mem_load_l3_hit_retired.xsnp_hitm,machine_clears.memory_ordering,mem_inst_retired.lock_loads,context-switches \
                -- "$RELEASE_BIN/queue_bench" one "$impl" 20000000 > /dev/null
            per_cpu_rows "$impl,$run,20000000" "$raw" >> "$CSV"
        done
    done
fi

if wants book; then
    wait_for_ac
    note "book: every variant on the real records, book only (15 passes each)"
    taskset -c "$PRIMARY_CPU" "$REPLAY" "$plain" x 0 "" 40 0 variants all | awk -v f="$NAME" 'NR == 1 { print "file," $0; next } { print f "," $0 }' \
        > "$DATA/inner_book_variants.csv"
    note "book: exporting the real records as a book_bench flow"
    flow=$FLOWS/$NAME.flow
    "$REPLAY" "$plain" x 0 "$flow" 40 0 export > "$DATA/flow_export.txt"
    note "book: per-operation latency"
    "$RELEASE_BIN/book_bench" latency --flow "$flow" --impl agg-map,agg-flat,ladder --runs 3 --scenario "$NAME" \
        > "$RUN_DIR/book_latency.raw"
    cp "$RUN_DIR/book_latency.raw" "$DATA/inner_book_op_latency.csv"
    note "book: top-down metrics and memory counters, counted only inside the timed loop"
    TMA=$DATA/inner_book_counters.csv
    echo "book,kind,name,value,ops" > "$TMA"
    ops=$("$RELEASE_BIN/book_bench" describe --flow "$flow" | grep -o 'timed_ops=[0-9]*' | cut -d= -f2)
    for book in agg-map agg-flat ladder; do
        for metric in tma_retiring tma_bad_speculation tma_frontend_bound tma_backend_bound tma_memory_bound \
            tma_core_bound tma_l1_bound tma_l2_bound tma_l3_bound tma_dram_bound tma_store_bound tma_branch_mispredicts; do
            raw=$RUN_DIR/book_${book}_$metric.perf
            gated "$raw" /dev/null -M "$metric" -- taskset -c "$PRIMARY_CPU" "$RELEASE_BIN/book_bench" throughput --flow "$flow" \
                --impl "$book" --runs 6 --no-header
            value=$(awk -F, -v m="$metric" '$7 ~ m { print $6; exit }' "$raw")
            echo "$book,metric,$metric,$value,$((ops * 6))" >> "$TMA"
        done
        for group in "cycles,instructions,branch-misses,mem_load_retired.l3_miss" \
            "cycles,mem_load_retired.l1_miss,mem_load_retired.l2_miss,cycle_activity.stalls_mem_any"; do
            raw=$RUN_DIR/book_${book}_counts.perf
            gated "$raw" /dev/null -e "$group" -- taskset -c "$PRIMARY_CPU" "$RELEASE_BIN/book_bench" throughput --flow "$flow" \
                --impl "$book" --runs 6 --no-header
            awk -F, -v OFS=, -v book="$book" -v ops="$((ops * 6))" '/^[0-9]/ { e = $3; gsub(/^cpu\/|\/$/, "", e); print book, "count", e, $1, ops }' "$raw" >> "$TMA"
        done
    done
    note "book: which data structure the slow loads (>= 30 cycles) belong to"
    MEM=$DATA/inner_book_slow_loads.csv
    echo "scenario,impl,source,owner,category,level,loads,latency_cycles" > "$MEM"
    for book in agg-map agg-flat ladder; do
        data=$RUN_DIR/book_${book}_mem.data
        dir=$(mktemp -d)
        mkfifo "$dir/ctl" "$dir/ack"
        exec {ctl}<> "$dir/ctl"
        exec {ack}<> "$dir/ack"
        LOB_PERF_CTL_FD=$ctl LOB_PERF_ACK_FD=$ack perf record -q -e cpu/mem-loads,ldlat=30/P -c 199 -d -W -D -1 \
            --control "fd:$ctl,$ack" -o "$data" -- taskset -c "$PRIMARY_CPU" "$RELEASE_BIN/book_bench" throughput \
            --flow "$flow" --impl "$book" --runs 4 --no-header > /dev/null 2>&1
        exec {ctl}>&-
        exec {ack}>&-
        perf script -i "$data" -F event,data_src,weight,ip,srcline 2> /dev/null |
            "$PYTHON" "$ROOT/tools/linux/perf_mem_sources.py" "$ROOT" "$NAME" "$book" >> "$MEM"
    done
fi

if wants flow; then
    wait_for_ac
    note "flow: whole-flow counters, flat out, ladder book, counted inside the replay loop"
    CSV=$DATA/flow_counters.csv
    echo "handler,mode,handoff,group,run,records,cpu,role,event,value,counted_pct" > "$CSV"
    for handler in "${HANDLERS[@]}"; do
        read -r tag mode handoff <<< "$handler"
        for group in "basic cycles,instructions,ref-cycles,msr/tsc/,context-switches,branch-misses" \
            "coherence cycles,mem_load_l3_hit_retired.xsnp_hitm,mem_inst_retired.lock_loads,machine_clears.memory_ordering" \
            "memory cycles,mem_load_retired.l2_miss,mem_load_retired.l3_miss,cycle_activity.stalls_l3_miss"; do
            read -r name events <<< "$group"
            for ((run = 0; run < RUNS; run++)); do
                raw=$RUN_DIR/flow_${tag}_${name}_$run.perf
                out=$RUN_DIR/flow_${tag}_${name}_$run.out
                gated "$raw" "$out" -C "$PAIR" -A -e "$events" -- \
                    env LOB_CPUS="$PAIR" "$REPLAY" "$plain" x 0 "" 40 0 "$mode" ladder-cached64 64 "$handoff"
                records=$(replay_fields "$out" | cut -d, -f1)
                per_cpu_rows "$tag,$mode,$handoff,$name,$run,$records" "$raw" >> "$CSV"
            done
        done
        note "flow: sampling where the cycles go, $tag"
        data=$RUN_DIR/flow_${tag}_cycles.data
        dir=$(mktemp -d)
        mkfifo "$dir/ctl" "$dir/ack"
        exec {ctl}<> "$dir/ctl"
        exec {ack}<> "$dir/ack"
        LOB_PERF_CTL_FD=$ctl LOB_PERF_ACK_FD=$ack perf record -q -F 4999 -C "$PAIR" -D -1 --control "fd:$ctl,$ack" \
            -o "$data" -- env LOB_CPUS="$PAIR" "$REPLAY" "$plain" x 0 "" 40 0 "$mode" ladder-cached64 64 "$handoff" \
            > /dev/null 2>&1
        exec {ctl}>&-
        exec {ack}>&-
        perf report -i "$data" --stdio --sort cpu,dso,sym -F overhead,cpu,dso,sym 2> /dev/null |
            "$PYTHON" -c '
import csv, re, sys
writer = csv.writer(sys.stdout)
for line in sys.stdin:
    m = re.match(r"\s+([0-9.]+)%\s+(\d+)\s+(\S+)\s+\[(.)\]\s+(.*\S)", line)
    if m:
        writer.writerow([sys.argv[1], m.group(2), m.group(3), m.group(4), m.group(5), m.group(1)])' "$tag" \
            >> "$RUN_DIR/flow_stages.tmp"
    done
    { echo "handler,cpu,dso,space,symbol,overhead_pct"; cat "$RUN_DIR/flow_stages.tmp"; } > "$DATA/flow_stages.csv"
    : > "$RUN_DIR/flow_stages.tmp"
fi

if wants combos_throughput; then
    wait_for_ac
    note "combos: throughput, every handoff x inner book, $RUNS runs"
    CSV=$DATA/combos_throughput.csv
    echo "handler,mode,handoff,book,book_variant,run,records,seconds,msgs_per_sec" > "$CSV"
    combos=()
    for handler in "${HANDLERS[@]}"; do
        for book in "${BOOKS[@]}"; do
            combos+=("$handler $book")
        done
    done
    for ((run = 0; run < RUNS; run++)); do
        order=("${combos[@]}")
        ((run % 2 == 1)) && mapfile -t order < <(printf '%s\n' "${combos[@]}" | tac)
        for combo in "${order[@]}"; do
            read -r tag mode handoff label variant <<< "$combo"
            out=$RUN_DIR/combo_${tag}_${label}_$run.out
            LOB_CPUS=$PAIR "$REPLAY" "$plain" x 0 "" 40 0 "$mode" "$variant" 64 "$handoff" > "$out"
            echo "$tag,$mode,$handoff,$label,$variant,$run,$(replay_fields "$out")" >> "$CSV"
        done
    done
fi

if wants idle_throughput; then
    wait_for_ac
    note "idle: throughput with 1 / 2 / 4 order books on the same 2 cores, $RUNS runs"
    CSV=$DATA/idle_throughput.csv
    echo "books,strategy,handoff,run,book_index,records,seconds,msgs_per_sec" > "$CSV"
    for ((run = 0; run < RUNS; run++)); do
        for books in 1 2 4; do
            for strategy in "${STRATEGIES[@]}"; do
                read -r label handoff <<< "$strategy"
                prefix=$RUN_DIR/idle_tp_${books}_${label}_$run
                "$SELF" --launch-books "$books" "$handoff" 0 "$prefix" "$plain"
                for ((k = 0; k < books; k++)); do
                    echo "$books,$label,$handoff,$run,$k,$(replay_fields "${prefix}_book$k.out")" >> "$CSV"
                done
            done
        done
    done
fi

merge_latency() {
    "$PYTHON" - "$@" << 'EOF'
import csv, os, sys
out, raw, prefix_header, prefix_values, *latency_files = sys.argv[1:]
busy, switches = {}, {}
for line in open(raw):
    f = line.strip().split(",")
    if len(f) > 3 and f[0].startswith("CPU"):
        event = f[3].replace("cpu/", "").rstrip("/")
        value = float(f[1]) if f[1].replace(".", "").isdigit() else 0.0
        busy.setdefault(f[0], {})[event] = value
cpu_busy = {cpu: 100 * e.get("ref-cycles", 0) / e["msr/tsc"] for cpu, e in busy.items() if e.get("msr/tsc")}
cpu_switches = {cpu: e.get("context-switches", 0) for cpu, e in busy.items()}
fresh = not os.path.exists(out) or os.path.getsize(out) == 0
with open(out, "a", newline="") as f:
    writer = csv.writer(f)
    for index, path in enumerate(latency_files):
        rows = list(csv.DictReader(open(path)))
        if not rows:
            continue
        row = rows[-1]
        if fresh:
            writer.writerow(prefix_header.split(",") + ["book_index", "cpu_busy_pct", "context_switches"] + list(row.keys()))
            fresh = False
        writer.writerow(prefix_values.split(",") + [index, ";".join(f"{c}={v:.1f}" for c, v in sorted(cpu_busy.items())),
                                                    ";".join(f"{c}={v:.0f}" for c, v in sorted(cpu_switches.items()))]
                        + list(row.values()))
EOF
}

if wants combos_latency; then
    CSV=$DATA/combos_latency.csv
    : > "$CSV"
    for ((run = 0; run < LATENCY_RUNS; run++)); do
        for handler in "${HANDLERS[@]}"; do
            read -r tag mode handoff <<< "$handler"
            for book in "${BOOKS[@]}"; do
                read -r label variant <<< "$book"
                wait_for_ac
                note "combos latency run $((run + 1))/$LATENCY_RUNS: $tag + $label, ${SPEED}x"
                id=combo_lat_${tag}_${label}_$run
                latency=$RUN_DIR/$id.csv
                raw=$RUN_DIR/$id.perf
                rm -f -- "$latency" "$latency".*
                perf stat -x, -o "$raw" -C "$PAIR" -A -e ref-cycles,msr/tsc/,context-switches -- \
                    env LOB_CPUS="$PAIR" "$REPLAY" "$plain" x 0 "" 40 "$SPEED" "$mode" "$variant" 64 "$handoff" \
                    "$latency" > /dev/null
                merge_latency "$CSV" "$raw" "handler,book,run" "$tag,$label,$run" "$latency"
            done
        done
    done
fi

if wants idle_latency; then
    CSV=$DATA/idle_latency.csv
    : > "$CSV"
    for ((run = 0; run < LATENCY_RUNS; run++)); do
        for books in 1 2 4; do
            for strategy in "${STRATEGIES[@]}"; do
                read -r label handoff <<< "$strategy"
                wait_for_ac
                note "idle latency run $((run + 1))/$LATENCY_RUNS: $books order book(s) on 2 cores, $label, ${SPEED}x"
                prefix=$RUN_DIR/idle_lat_${books}_${label}_$run
                raw=$prefix.perf
                rm -f -- "${prefix}"_book*.csv "${prefix}"_book*.csv.*
                perf stat -x, -o "$raw" -C "$PAIR" -A -e ref-cycles,msr/tsc/,context-switches -- \
                    "$SELF" --launch-books "$books" "$handoff" "$SPEED" "$prefix" "$plain"
                files=()
                for ((k = 0; k < books; k++)); do
                    files+=("${prefix}_book$k.csv")
                done
                merge_latency "$CSV" "$raw" "books,strategy,handoff,run" "$books,$label,$handoff,$run" "${files[@]}"
            done
        done
    done
fi

note "results in $DATA"
