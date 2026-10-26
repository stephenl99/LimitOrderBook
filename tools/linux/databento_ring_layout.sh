#!/usr/bin/env bash
set -euo pipefail
SELF=$(realpath "${BASH_SOURCE[0]}")
source "$(dirname "$SELF")/common.sh"

usage() {
    cat << EOF
usage: tools/linux/databento_ring_layout.sh [--runs N] [--latency-runs N] [--speed X]

The SPSC ring three ways, everything else default (back-off, ladder-cached64, batch 64, CPUs 1 and 2):
  seq_cst, unpadded   every atomic seq_cst, head and tail on one cache line   (ring-seqcst-unpadded)
  acq/rel, unpadded   acquire/release, head and tail on one cache line        (ring-unpadded)
  acq/rel, padded     acquire/release, head and tail on their own lines       (ring, the default)
Writes ring_layout_throughput.csv, ring_layout_counters.csv and ring_layout_latency.csv to
\$OUT/databento/final-demo_data.
EOF
}

RUNS=5
LATENCY_RUNS=2
SPEED=60
while (($#)); do
    case $1 in
    --runs) RUNS=$2; shift ;;
    --latency-runs) LATENCY_RUNS=$2; shift ;;
    --speed) SPEED=$2; shift ;;
    -h | --help) usage; exit 0 ;;
    *) usage; die "unknown option $1" ;;
    esac
    shift
done

on_ac() {
    local online
    online=$(cat /sys/class/power_supply/AC*/online 2> /dev/null || echo 1)
    [[ $online != 0 ]]
}

wait_for_ac() {
    on_ac && return
    note "on battery: paused until the charger is back"
    until on_ac; do
        sleep 10
    done
}

[[ $(perf_scope) == all ]] || die "needs perf counters: sudo sysctl kernel.perf_event_paranoid=0"
setup_cpus
build_release databento_replay
REPLAY=$RELEASE_BIN/databento_replay
DATA=$OUT/databento/final-demo_data
RUN_DIR=$DATA/runs
mkdir -p "$RUN_DIR"
plain=$OUT/databento/data/cme_mix.mbo.dbn
[[ -f $plain ]] || zstd -q -d -f "$ROOT/testdata/cme_mix.mbo.dbn.zst" -o "$plain"
PAIR=$PRIMARY_CPU,$CROSS_CPU
VARIANTS=("seqcst-unpadded ring-seqcst-unpadded" "acqrel-unpadded ring-unpadded" "acqrel-padded ring")

TP=$DATA/ring_layout_throughput.csv
COUNTERS=$DATA/ring_layout_counters.csv
echo "variant,handoff,run,records,seconds,msgs_per_sec" > "$TP"
echo "variant,run,records,cpu,role,event,value,counted_pct" > "$COUNTERS"
for ((run = 0; run < RUNS; run++)); do
    for variant in "${VARIANTS[@]}"; do
        read -r tag handoff <<< "$variant"
        wait_for_ac
        note "ring layout throughput run $((run + 1))/$RUNS: $tag"
        out=$RUN_DIR/ring_layout_${tag}_$run.out
        raw=$RUN_DIR/ring_layout_${tag}_$run.perf
        dir=$(mktemp -d)
        mkfifo "$dir/ctl" "$dir/ack"
        exec {ctl}<> "$dir/ctl"
        exec {ack}<> "$dir/ack"
        LOB_PERF_CTL_FD=$ctl LOB_PERF_ACK_FD=$ack perf stat -x, -o "$raw" -D -1 --control "fd:$ctl,$ack" -C "$PAIR" -A \
            -e cycles,mem_load_l3_hit_retired.xsnp_hitm,mem_inst_retired.lock_loads,machine_clears.memory_ordering -- \
            env LOB_CPUS="$PAIR" "$REPLAY" "$plain" x 0 "" 40 0 ring ladder-cached64 64 "$handoff" > "$out"
        exec {ctl}>&-
        exec {ack}>&-
        fields=$("$PYTHON" -c '
import re, sys
m = re.search(r"replayed (\d+) records in ([0-9.e+-]+)s \((\d+) msgs/s", open(sys.argv[1]).read())
print(",".join(m.groups()))' "$out")
        echo "$tag,$handoff,$run,$fields" >> "$TP"
        awk -F, -v OFS=, -v prefix="$tag,$run,${fields%%,*}" -v producer="CPU$PRIMARY_CPU" -v consumer="CPU$CROSS_CPU" '
            /^CPU/ { role = $1 == producer ? "producer" : ($1 == consumer ? "consumer" : "other")
                     event = $4; gsub(/^cpu\/|\/$/, "", event)
                     print prefix, $1, role, event, ($2 ~ /^</ ? "" : $2), $6 }' "$raw" >> "$COUNTERS"
    done
done

LAT=$DATA/ring_layout_latency.csv
: > "$LAT"
for ((run = 0; run < LATENCY_RUNS; run++)); do
    for variant in "${VARIANTS[@]}"; do
        read -r tag handoff <<< "$variant"
        wait_for_ac
        note "ring layout latency run $((run + 1))/$LATENCY_RUNS: $tag, ${SPEED}x"
        latency=$RUN_DIR/ring_layout_lat_${tag}_$run.csv
        rm -f -- "$latency" "$latency".*
        LOB_CPUS=$PAIR "$REPLAY" "$plain" x 0 "" 40 "$SPEED" ring ladder-cached64 64 "$handoff" "$latency" > /dev/null
        "$PYTHON" - "$LAT" "$tag" "$run" "$latency" << 'EOF'
import csv, os, sys
out, tag, run, path = sys.argv[1:]
row = list(csv.DictReader(open(path)))[-1]
fresh = not os.path.getsize(out)
with open(out, "a", newline="") as f:
    writer = csv.writer(f)
    if fresh:
        writer.writerow(["variant", "run"] + list(row.keys()))
    writer.writerow([tag, run] + list(row.values()))
EOF
    done
done
note "results in $DATA"
