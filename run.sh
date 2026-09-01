#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"
./configure.sh
meson compile -C build
exec ./build/app "$@"
