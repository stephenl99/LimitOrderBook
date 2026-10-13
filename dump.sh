#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"
make itch_dump
exec ./build-cmake/debug/itch_dump "${1:-testdata/session_mix.bin}"
