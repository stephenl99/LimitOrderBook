#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"
./configure.sh
meson compile -C build core_test
exec ./build/core_test "$@"
