#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"

if [[ -d build ]]; then
  meson setup build --reconfigure
else
  meson setup build
fi
ln -sfn build/compile_commands.json compile_commands.json
meson compile -C build itch_dump
exec ./build/itch_dump "${1:-testdata/session_mix.bin}"
