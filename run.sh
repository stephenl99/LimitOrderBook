#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"
make app
exec ./build-make/bin/app "$@"
