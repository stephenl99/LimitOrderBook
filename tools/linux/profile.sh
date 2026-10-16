#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

usage() {
    cat << EOF
usage: tools/linux/profile.sh [options]

Hardware-counter and sampling profiles of book_bench and queue_bench. Nothing is
inserted in the code beyond book_bench's counter gate (LOB_PERF_CTL_FD): every
step runs the release binaries under perf. Writes to \$OUT/perf (default
benchmarks/linux/perf):

  book_counters.csv   scenario x book, one row per event: counted in the timed loop only
  queue_counters.csv  placement x queue x side (producer / consumer, counted per CPU)
  book_mem.csv        sampled slow loads (>= 30 cycles) per source line: level that served it, latency
  book_hotspots.csv   self time per function
  clock.csv           effective clock of the benchmark CPU every 250 ms during a long run
  profiles/*.perf.gz  perf script output, open in https://profiler.firefox.com

  --scenarios a,b     book scenarios for the counter pass (default equity,deep,multi)
  --sample-impls a,b  books for the sampling passes (default tree,agg-flat,ladder,pooled-ladder,lazy-handle-ladder)
  --only a,b          subset of steps: book,queue,mem,cpu,clock (default all)
  --runs N            book_bench passes per measurement (default 2)
  --no-plot           skip the charts
env: PRIMARY_CPU / CROSS_CPU / SMT_CPU, OUT=dir
EOF
}

SCENARIOS=equity,deep,multi
SAMPLE_IMPLS=tree,agg-flat,ladder,pooled-ladder,lazy-handle-ladder
ONLY=book,queue,mem,cpu,clock
RUNS=2
PLOT=1
while (($#)); do
    case $1 in
    --scenarios) SCENARIOS=$2; shift ;;
    --sample-impls) SAMPLE_IMPLS=$2; shift ;;
    --only) ONLY=$2; shift ;;
    --runs) RUNS=$2; shift ;;
    --no-plot) PLOT=0 ;;
    -h | --help) usage; exit 0 ;;
    *) usage; die "unknown option $1" ;;
    esac
    shift
done

wants() {
    [[ ,$ONLY, == *",$1,"* ]]
}

[[ $(perf_scope) == all ]] || die "needs perf_event_paranoid <= 1 (now $(cat /proc/sys/kernel/perf_event_paranoid)): sudo sysctl kernel.perf_event_paranoid=0"
setup_cpus
build_release book_bench queue_bench
BOOK=$RELEASE_BIN/book_bench
QUEUE=$RELEASE_BIN/queue_bench
PERF_OUT=$OUT/perf
mkdir -p "$PERF_OUT/profiles" "$OUT/flows"
machine_info > "$PERF_OUT/env.txt"
echo "nmi_watchdog: $(cat /proc/sys/kernel/nmi_watchdog)" >> "$PERF_OUT/env.txt"
warn_noise
if [[ $(cat /proc/sys/kernel/nmi_watchdog) == 1 ]]; then
    note "the NMI watchdog holds a fixed counter, so groups are kept to 4 hardware events (sudo sysctl kernel.nmi_watchdog=0 frees it)"
fi

IFS=, read -r -a scenario_list <<< "$SCENARIOS"
IFS=, read -r -a sample_list <<< "$SAMPLE_IMPLS"
PIN=(taskset -c "$PRIMARY_CPU")

declare -A EVENT_GROUPS=(
    [basic]="task-clock,cycles,instructions,ref-cycles,branches,branch-misses"
    [slots]="cycles,uops_issued.any,uops_retired.retire_slots"
    [frontend]="cycles,idq_uops_not_delivered.core,int_misc.recovery_cycles_any"
    [misses]="cycles,mem_load_retired.l1_miss,mem_load_retired.l2_miss,mem_load_retired.l3_miss"
    [stalls]="cycles,cycle_activity.stalls_l1d_miss,cycle_activity.stalls_l2_miss,cycle_activity.stalls_l3_miss"
)

scenario_flow() {
    local scenario=$1 file=$OUT/flows/$1.flow args
    if [[ ! -f $file ]]; then
        case $scenario in
        equity) args="--resting 5000" ;;
        deep) args="--resting 50000" ;;
        multi) args="--resting 2000 --instruments 64" ;;
        cme6) args="--resting 5000 --instruments 6" ;;
        random_ids) args="--resting 5000 --random-ids" ;;
        *) die "no flow file $file, and $scenario is not a generated scenario (equity, deep, multi, cme6, random_ids)" ;;
        esac
        read -r -a args <<< "$args"
        "$BOOK" generate --ops 2000000 "${args[@]}" --out "$file" > /dev/null
    fi
    echo "$file"
}

gated_book() {
    local mode=$1 out=$2 flow=$3 impl=$4
    shift 4
    local dir ctl ack
    dir=$(mktemp -d)
    mkfifo "$dir/ctl" "$dir/ack"
    exec {ctl}<> "$dir/ctl"
    exec {ack}<> "$dir/ack"
    LOB_PERF_CTL_FD=$ctl LOB_PERF_ACK_FD=$ack perf "$mode" "$@" -D -1 --control "fd:$ctl,$ack" -o "$out" -- \
        "${PIN[@]}" "$BOOK" throughput --flow "$flow" --impl "$impl" --runs "$RUNS" --no-header > /dev/null
    exec {ctl}>&-
    exec {ack}>&-
    rm -rf "$dir"
}

normalize_rows() {
    awk -F, -v prefix="$1" '
        /^#/ || NF < 4 { next }
        { value = $1; event = $3
          gsub(/^cpu\/|\/$|:u$/, "", event)
          if (value ~ /^</) value = ""
          print prefix "," event "," value "," $5 }'
}

if wants book; then
    BOOK_CSV=$PERF_OUT/book_counters.csv
    echo "scenario,impl,ops,group,event,value,counted_pct" > "$BOOK_CSV"
    impls=$("$BOOK" list | cut -d, -f1)
    for scenario in "${scenario_list[@]}"; do
        flow=$(scenario_flow "$scenario")
        ops=$(($("$BOOK" describe --flow "$flow" | grep -o 'timed_ops=[0-9]*' | cut -d= -f2) * RUNS))
        for impl in $impls; do
            note "counters: $scenario / $impl"
            for group in basic slots frontend misses stalls; do
                raw=$(mktemp)
                gated_book stat "$raw" "$flow" "$impl" -x, -e "${EVENT_GROUPS[$group]}"
                normalize_rows "$scenario,$impl,$ops,$group" < "$raw" >> "$BOOK_CSV"
                rm -f "$raw"
            done
        done
    done
fi

if wants queue; then
    QUEUE_CSV=$PERF_OUT/queue_counters.csv
    echo "placement,impl,items,side,event,value,counted_pct" > "$QUEUE_CSV"
    items=20000000
    for placement in cross_core smt; do
        if [[ $placement == smt && -z $SMT_CPU ]]; then
            continue
        fi
        if [[ $placement == cross_core ]]; then
            producer=$PRIMARY_CPU consumer=$CROSS_CPU
        else
            producer=$PRIMARY_CPU consumer=$SMT_CPU
        fi
        for impl in mutex ring_yield ring_spin ring uncached_index sequenced mpmc; do
            note "counters: queue $impl, $placement (cpu$producer -> cpu$consumer)"
            raw=$(mktemp)
            LOB_CPUS=$producer,$consumer perf stat -x, -o "$raw" -C "$producer,$consumer" -A \
                -e cycles,instructions,mem_load_l3_hit_retired.xsnp_hitm,machine_clears.memory_ordering,mem_inst_retired.lock_loads,context-switches \
                -- "$QUEUE" one "$impl" "$items" > /dev/null
            awk -F, -v OFS=, -v placement="$placement" -v impl="$impl" -v items="$items" \
                -v producer="CPU$producer" -v consumer="CPU$consumer" '
                /^CPU/ { side = $1 == producer ? "producer" : ($1 == consumer ? "consumer" : "")
                         if (side == "") next
                         event = $4; gsub(/^cpu\/|\/$/, "", event)
                         value = $2 ~ /^</ ? "" : $2
                         print placement, impl, items, side, event, value, $6 }' "$raw" >> "$QUEUE_CSV"
            rm -f "$raw"
        done
    done
fi

if wants mem; then
    MEM_CSV=$PERF_OUT/book_mem.csv
    echo "scenario,impl,source,owner,category,level,loads,latency_cycles" > "$MEM_CSV"
    for scenario in "${scenario_list[@]}"; do
        flow=$(scenario_flow "$scenario")
        for impl in "${sample_list[@]}"; do
            note "slow loads: $scenario / $impl"
            data=$(mktemp)
            gated_book record "$data" "$flow" "$impl" -q -e cpu/mem-loads,ldlat=30/P -c 199 -d -W
            perf script -i "$data" -F event,data_src,weight,ip,srcline 2> /dev/null |
                "$PYTHON" "$ROOT/tools/linux/perf_mem_sources.py" "$ROOT" "$scenario" "$impl" >> "$MEM_CSV"
            rm -f "$data"
        done
    done
fi

if wants cpu; then
    HOT_CSV=$PERF_OUT/book_hotspots.csv
    echo "scenario,impl,symbol,self_pct" > "$HOT_CSV"
    scenario=${scenario_list[0]}
    flow=$(scenario_flow "$scenario")
    for impl in "${sample_list[@]}"; do
        note "call stacks: $scenario / $impl"
        data=$(mktemp)
        gated_book record "$data" "$flow" "$impl" -q -F 2999 --call-graph lbr
        perf report -i "$data" --no-children --stdio -g none --percent-limit 0.5 -F overhead,sym 2> /dev/null |
            "$PYTHON" -c '
import csv, re, sys
writer = csv.writer(sys.stdout)
for line in sys.stdin:
    match = re.match(r"\s+([0-9.]+)%\s+\[[^]]+\]\s+(.*\S)", line)
    if match:
        writer.writerow([sys.argv[1], sys.argv[2], match.group(2), match.group(1)])
' "$scenario" "$impl" >> "$HOT_CSV"
        perf script -i "$data" 2> /dev/null | gzip -9 > "$PERF_OUT/profiles/$scenario-$impl.perf.gz"
        rm -f "$data"
    done
fi

if wants clock; then
    note "clock trace: every book on the ${scenario_list[0]} flow, back to back"
    flow=$(scenario_flow "${scenario_list[0]}")
    raw=$(mktemp)
    perf stat -x, -I 250 -C "$PRIMARY_CPU" -e cycles,ref-cycles -o "$raw" -- \
        "${PIN[@]}" "$BOOK" throughput --flow "$flow" --impl all --runs 3 --no-header > /dev/null
    nominal=$(grep -m1 'model name' /proc/cpuinfo | grep -oE '[0-9.]+GHz' | tr -d GHz)
    echo "time_s,cycles,ref_cycles,nominal_ghz" > "$PERF_OUT/clock.csv"
    awk -F, -v OFS=, -v nominal="$nominal" '
        /^ *[0-9]/ { t = $1 + 0; event = $4; gsub(/^cpu\/|\/$/, "", event); v[t, event] = $2; times[t] = 1 }
        END { for (t in times) print t, v[t, "cycles"], v[t, "ref-cycles"], nominal }' "$raw" | sort -t, -k1,1n \
        >> "$PERF_OUT/clock.csv"
    rm -f "$raw"
fi

if ((PLOT)); then
    "$PYTHON" "$ROOT/tools/plot_linux_perf.py" "$PERF_OUT"
fi
note "results in $PERF_OUT"
