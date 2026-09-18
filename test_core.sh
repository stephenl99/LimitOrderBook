#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"
make core_test
exec ./build-make/bin/core_test "$@"
