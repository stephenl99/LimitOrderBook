#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"

BUILD=build-release
REPEATS="${1:-20}"
FIXTURE="${2:-testdata/big_mix.bin}"

if [[ ! -d "$BUILD" ]]; then
  meson setup "$BUILD" -Dbuildtype=release
fi
meson compile -C "$BUILD" bench

if [[ ! -f "$FIXTURE" ]]; then
  echo "bench: generating $FIXTURE ..."
  python3 testdata/gen_big_mix.py --out "$FIXTURE"
fi

exec "./$BUILD/bench" "$REPEATS" "$FIXTURE"
