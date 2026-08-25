#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"

if [[ -d build ]]; then
  meson setup build --reconfigure
else
  meson setup build
fi
ln -sfn build/compile_commands.json compile_commands.json
meson compile -C build core_test
exec ./build/core_test "$@"
