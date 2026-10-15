#!/usr/bin/env bash

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
RELEASE_BIN=$ROOT/build-cmake/release
OUT=${OUT:-$ROOT/benchmarks/linux}
PYTHON=${PYTHON:-python3}

die() {
    echo "error: $*" >&2
    exit 1
}

note() {
    echo "== $*" >&2
}

build_release() {
    command -v cmake > /dev/null || die "cmake not found (sudo apt install cmake ninja-build)"
    command -v ninja > /dev/null || die "ninja not found (sudo apt install ninja-build)"
    if [[ ! -f $RELEASE_BIN/build.ninja ]]; then
        (cd "$ROOT" && cmake --preset release > /dev/null)
    fi
    (cd "$ROOT" && cmake --build --preset release --target "$@")
}

siblings_of() {
    local list
    list=$(cat "/sys/devices/system/cpu/cpu$1/topology/thread_siblings_list")
    echo "$list" | tr ',' '\n' | while IFS=- read -r lo hi; do
        seq "$lo" "${hi:-$lo}"
    done
}

smt_sibling_of() {
    siblings_of "$1" | grep -vx "$1" | head -n 1
}

other_core_cpu() {
    local base=$1 cpu
    local busy
    busy=$(siblings_of "$base" | tr '\n' ' ')
    for cpu in $(seq 1 $(($(nproc) - 1))); do
        if [[ " $busy " != *" $cpu "* ]]; then
            echo "$cpu"
            return
        fi
    done
}

physical_first_cpus() {
    local cpu first=() second=()
    for cpu in $(seq "$1" $(($(nproc) - 1))); do
        if [[ $(siblings_of "$cpu" | head -n 1) == "$cpu" ]]; then
            first+=("$cpu")
        else
            second+=("$cpu")
        fi
    done
    echo "${first[@]}" "${second[@]}" | tr ' ' ','
}

setup_cpus() {
    PRIMARY_CPU=${PRIMARY_CPU:-1}
    CROSS_CPU=${CROSS_CPU:-$(other_core_cpu "$PRIMARY_CPU")}
    SMT_CPU=${SMT_CPU:-$(smt_sibling_of "$PRIMARY_CPU")}
}

warn_noise() {
    local governor turbo
    governor=$(cat /sys/devices/system/cpu/cpu"$PRIMARY_CPU"/cpufreq/scaling_governor 2> /dev/null || echo unknown)
    if [[ $governor != performance ]]; then
        note "governor is '$governor'; for steadier numbers: sudo cpupower frequency-set -g performance"
    fi
    turbo=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2> /dev/null || echo "")
    if [[ $turbo == 0 ]]; then
        note "turbo is on, clocks will drift with temperature; to pin: echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo"
    fi
    if [[ $(cat /sys/class/power_supply/AC*/online 2> /dev/null | head -n 1) == 0 ]]; then
        note "on battery power"
    fi
}

perf_scope() {
    local paranoid
    paranoid=$(cat /proc/sys/kernel/perf_event_paranoid)
    if ! command -v perf > /dev/null; then
        echo none
    elif ((paranoid <= 1)); then
        echo all
    elif ((paranoid == 2)); then
        echo user
    else
        echo none
    fi
}

perf_usable() {
    [[ ${PERF:-1} != 0 && $(perf_scope) != none ]] && perf stat -e task-clock -x, true > /dev/null 2>&1
}

explain_perf() {
    if [[ ${PERF:-1} == 0 ]]; then
        note "PERF=0, skipping hardware counters"
    elif ! command -v perf > /dev/null; then
        note "perf is not installed (sudo apt install linux-tools-\$(uname -r)), skipping hardware counters"
    else
        note "perf_event_paranoid is $(cat /proc/sys/kernel/perf_event_paranoid), skipping hardware counters." \
            "Allow them with: sudo sysctl kernel.perf_event_paranoid=1"
    fi
}

has_event() {
    perf list --no-desc 2> /dev/null | grep -qw -- "$1"
}

perf_events() {
    local suffix="" events
    [[ $(perf_scope) == user ]] && suffix=":u"
    events="task-clock,context-switches,cycles$suffix,instructions$suffix,branch-misses$suffix"
    events+=",L1-dcache-load-misses$suffix,LLC-loads$suffix,LLC-load-misses$suffix"
    local extra
    for extra in mem_load_l3_hit_retired.xsnp_hitm mem_load_l3_miss_retired.remote_hitm \
        dTLB-load-misses; do
        if has_event "$extra"; then
            events+=",$extra$suffix"
        fi
    done
    echo "$events"
}

perf_stat_gated() {
    local out=$1
    shift
    local fifo_dir ctl ack
    fifo_dir=$(mktemp -d)
    mkfifo "$fifo_dir/ctl" "$fifo_dir/ack"
    exec {ctl}<> "$fifo_dir/ctl"
    exec {ack}<> "$fifo_dir/ack"
    LOB_PERF_CTL_FD=$ctl LOB_PERF_ACK_FD=$ack perf stat -x, -e "$(perf_events)" -o "$out" \
        -D -1 --control "fd:$ctl,$ack" -- "$@"
    local status=$?
    exec {ctl}>&-
    exec {ack}>&-
    rm -rf "$fifo_dir"
    return $status
}

perf_stat_plain() {
    local out=$1
    shift
    perf stat -x, -e "$(perf_events)" -o "$out" -- "$@"
}

perf_to_rows() {
    local prefix=$1 file=$2
    grep -v '^#' "$file" | grep -v '^$' | awk -F, -v prefix="$prefix" '
        { value = $1; event = $3; sub(/:u$/, "", event)
          if (value ~ /^</) value = ""
          print prefix "," event "," value "," $5 }'
}

machine_info() {
    {
        echo "date: $(date -Is)"
        echo "host: $(hostname)"
        echo "kernel: $(uname -r)"
        echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
        echo "cpus: $(nproc), smt siblings of cpu$PRIMARY_CPU: $(siblings_of "$PRIMARY_CPU" | tr '\n' ' ')"
        echo "pinning: primary=$PRIMARY_CPU cross_core=$CROSS_CPU smt=$SMT_CPU"
        echo "governor: $(cat /sys/devices/system/cpu/cpu"$PRIMARY_CPU"/cpufreq/scaling_governor 2> /dev/null || echo unknown)"
        echo "no_turbo: $(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2> /dev/null || echo unknown)"
        echo "compiler: $(c++ --version | head -n 1)"
        echo "commit: $(git -C "$ROOT" rev-parse --short HEAD)$(git -C "$ROOT" diff --quiet || echo ' (dirty)')"
        echo "perf_event_paranoid: $(cat /proc/sys/kernel/perf_event_paranoid)"
    }
}
