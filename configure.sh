#!/bin/zsh
# Regenerate Meson build dirs with C++23. Run after pulling or changing meson.build.
set -euo pipefail
cd "$(dirname "$0")"

meson setup build --reconfigure -Dbuildtype=debug -Dcpp_std=c++23
meson setup build-release --reconfigure -Dbuildtype=release -Dcpp_std=c++23
ln -sfn build/compile_commands.json compile_commands.json

echo "configured: build/ (debug, c++23), build-release/ (release, c++23)"
