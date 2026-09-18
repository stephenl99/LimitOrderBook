#!/bin/zsh
set -euo pipefail
cd "$(dirname "$0")"

# NOTE: the real GoogleTest suite (unit_tests) depended on meson's gtest
# subproject wiring, which is gone now that the Makefile is the build.
# This runs core_test against the checked-in fixtures as a smoke check
# until gtest is vendored some other way (e.g. built once via its own
# CMake, same pattern planned for databento-cpp).
make core_test

echo "--- smoke test: add_order_a.bin ---"
./build-make/bin/core_test testdata/add_order_a.bin

echo "--- smoke test: session_mix.bin ---"
./build-make/bin/core_test testdata/session_mix.bin
