#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"

REPEATS="${1:-20}"
FIXTURE="${2:-testdata/big_mix.bin}"

make RELEASE=1 bench

if [[ ! -f "$FIXTURE" ]]; then
  echo "bench: generating $FIXTURE ..."
  python3 testdata/gen_big_mix.py --out "$FIXTURE"
fi

exec ./build-cmake/release/bench "$REPEATS" "$FIXTURE"
