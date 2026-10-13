#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"
make core_test
exec lldb ./build-cmake/debug/core_test -- testdata/session_mix.bin "$@"
