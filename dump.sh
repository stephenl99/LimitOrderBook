#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"
make itch_dump
exec ./build-make/bin/itch_dump "${1:-testdata/session_mix.bin}"
