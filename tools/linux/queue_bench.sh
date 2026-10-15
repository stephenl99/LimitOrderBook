#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

usage() {
    cat << EOF
usage: tools/linux/queue_bench.sh [options]

Runs queue_bench with the producer and consumer pinned three ways:
  unpinned    the scheduler decides
  cross_core  two physical cores  (PRIMARY_CPU and CROSS_CPU)
  smt         two hyperthreads of one core  (PRIMARY_CPU and SMT_CPU)
Writes to \$OUT (default benchmarks/linux):
  queue_buffers.csv     ring buffer variants: flat out and paced latency
  queue_idle.csv        idle strategies (mutex, yield, spin, back-off): flat out
  queue_idle_latency.csv  idle strategies: paced latency
  queue_perf.csv        perf stat per buffer, per placement
  queue_c2c_*.txt       perf c2c reports (--c2c)
  queue_fanout.csv      one producer, N consumers (--fanout)

  --runs N            runs per variant (default 5)
  --items N           items per perf stat run (default 20000000)
  --placements a,b    subset of unpinned,cross_core,smt (default all three)
  --latency-placements a,b   placements for the idle-strategy latency pass (default cross_core,smt)
  --rates a,b         paced rates in msgs/s (default 10000,100000,1000000)
  --c2c               record perf c2c for ring and uncached_index (needs perf_event_paranoid <= 1, often root)
  --fanout            also run the 1 -> N consumer test
  --no-plot           skip the plots
env: PERF=0 skips perf, PRIMARY_CPU / CROSS_CPU / SMT_CPU override the CPUs, OUT=dir
EOF
}

RUNS=5
ITEMS=20000000
PLACEMENTS=unpinned,cross_core,smt
LATENCY_PLACEMENTS=cross_core,smt
RATES=10000,100000,1000000
C2C=0
FANOUT=0
PLOT=1
while (($#)); do
    case $1 in
    --runs) RUNS=$2; shift ;;
    --items) ITEMS=$2; shift ;;
    --placements) PLACEMENTS=$2; shift ;;
    --latency-placements) LATENCY_PLACEMENTS=$2; shift ;;
    --rates) RATES=$2; shift ;;
    --c2c) C2C=1 ;;
    --fanout) FANOUT=1 ;;
    --no-plot) PLOT=0 ;;
    -h | --help) usage; exit 0 ;;
    *) usage; die "unknown option $1" ;;
    esac
    shift
done

setup_cpus
[[ -n $SMT_CPU ]] || note "cpu$PRIMARY_CPU has no SMT sibling, the smt placement will be skipped"
build_release queue_bench
BENCH=$RELEASE_BIN/queue_bench
mkdir -p "$OUT"
machine_info > "$OUT/queue_env.txt"
warn_noise

cpus_for() {
    case $1 in
    unpinned) echo "" ;;
    cross_core) echo "$PRIMARY_CPU,$CROSS_CPU" ;;
    smt) echo "$PRIMARY_CPU,$SMT_CPU" ;;
    *) die "unknown placement $1" ;;
    esac
}

placement_ok() {
    [[ $1 != smt || -n $SMT_CPU ]]
}

prefix_rows() {
    awk -v prefix="$1" '{ print prefix "," $0 }'
}

IFS=, read -r -a placement_list <<< "$PLACEMENTS"
IFS=, read -r -a latency_placement_list <<< "$LATENCY_PLACEMENTS"
IFS=, read -r -a rate_list <<< "$RATES"

BUFFERS_CSV=$OUT/queue_buffers.csv
echo "placement,bench,impl,scenario,run,throughput_mps,p50_ns,p99_ns,p999_ns,consumer_cpu_pct" > "$BUFFERS_CSV"
for placement in "${placement_list[@]}"; do
    placement_ok "$placement" || continue
    note "buffers, $placement: flat out and paced at 1M / 100k / 10k msgs/s"
    LOB_CPUS=$(cpus_for "$placement") "$BENCH" variants "$RUNS" | prefix_rows "$placement" >> "$BUFFERS_CSV"
done

IDLE_CSV=$OUT/queue_idle.csv
IDLE_LATENCY_CSV=$OUT/queue_idle_latency.csv
echo "placement,bench,impl,size,run,ms" > "$IDLE_CSV"
echo "placement,bench,impl,rate,run,p50_ns,p99_ns,p999_ns,max_ns,consumer_cpu_pct" > "$IDLE_LATENCY_CSV"
for placement in "${latency_placement_list[@]}"; do
    placement_ok "$placement" || continue
    cpus=$(cpus_for "$placement")
    note "idle strategies, $placement: flat out"
    LOB_CPUS=$cpus "$BENCH" queue 5000000 "$RUNS" | prefix_rows "$placement" >> "$IDLE_CSV"
    for rate in "${rate_list[@]}"; do
        note "idle strategies, $placement: paced at $rate msgs/s"
        LOB_CPUS=$cpus "$BENCH" latency "$rate" "$RUNS" 1 | prefix_rows "$placement" >> "$IDLE_LATENCY_CSV"
    done
done

PERF_CSV=$OUT/queue_perf.csv
echo "placement,impl,items,event,value,counted_pct" > "$PERF_CSV"
if perf_usable; then
    for placement in cross_core smt; do
        placement_ok "$placement" || continue
        for impl in mutex ring_yield ring_spin ring uncached_index sequenced mpmc; do
            note "perf stat, $placement: $impl"
            raw=$(mktemp)
            LOB_CPUS=$(cpus_for "$placement") perf_stat_plain "$raw" "$BENCH" one "$impl" "$ITEMS" > /dev/null
            perf_to_rows "$placement,$impl,$ITEMS" "$raw" >> "$PERF_CSV"
            rm -f "$raw"
        done
    done
else
    explain_perf
fi

if ((C2C)); then
    for impl in ring uncached_index sequenced; do
        note "perf c2c: $impl"
        data=$(mktemp)
        if LOB_CPUS=$(cpus_for cross_core) perf c2c record -o "$data" -- "$BENCH" one "$impl" "$ITEMS" > /dev/null 2>&1; then
            perf c2c report -i "$data" --stdio --stats > "$OUT/queue_c2c_$impl.txt" 2> /dev/null || true
            perf c2c report -i "$data" --stdio -N 2> /dev/null | head -n 120 >> "$OUT/queue_c2c_$impl.txt" || true
        else
            note "perf c2c record failed (needs perf_event_paranoid <= 1 and load-latency events; try with sudo)"
        fi
        rm -f "$data"
    done
fi

if ((FANOUT)); then
    note "fanout: 1 producer, 1/2/4 consumers"
    all_cpus=$(physical_first_cpus "$PRIMARY_CPU")
    echo "bench,impl,consumers,run,mps" > "$OUT/queue_fanout.csv"
    LOB_CPUS=$all_cpus "$BENCH" fanout 10000000 "$RUNS" >> "$OUT/queue_fanout.csv"
fi

if ((PLOT)); then
    "$PYTHON" "$ROOT/tools/plot_linux_bench.py" queue "$OUT"
fi
note "results in $OUT"
