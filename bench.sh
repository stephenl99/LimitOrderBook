#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"

REPEATS="${1:-20}"
FIXTURE="${2:-testdata/big_mix.bin}"

./configure.sh
meson compile -C build-release bench

if [[ ! -f "$FIXTURE" ]]; then
  echo "bench: generating $FIXTURE ..."
  python3 testdata/gen_big_mix.py --out "$FIXTURE"
fi

exec ./build-release/bench "$REPEATS" "$FIXTURE"
