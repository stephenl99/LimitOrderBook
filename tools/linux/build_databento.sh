#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

VERSION=${DATABENTO_VERSION:-v0.68.1}
SOURCE=$ROOT/vendor/databento-cpp
INSTALL=$ROOT/vendor/install

[[ -f /usr/include/openssl/ssl.h ]] || die "OpenSSL headers missing: sudo apt install libssl-dev"
[[ -f /usr/include/zstd.h ]] || die "zstd headers missing: sudo apt install libzstd-dev"
command -v cmake > /dev/null || die "cmake not found (sudo apt install cmake ninja-build)"

if [[ ! -d $SOURCE ]]; then
    git clone --depth 1 --branch "$VERSION" https://github.com/databento/databento-cpp "$SOURCE"
fi
cmake -S "$SOURCE" -B "$SOURCE/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$INSTALL" \
    -DDATABENTO_ENABLE_UNIT_TESTING=OFF -DDATABENTO_ENABLE_EXAMPLES=OFF -DDATABENTO_WARNINGS_AS_ERRORS=OFF > /dev/null
cmake --build "$SOURCE/build"
cmake --install "$SOURCE/build" > /dev/null
rm -rf "$RELEASE_BIN/CMakeCache.txt"
note "databento-cpp $VERSION installed in $INSTALL; reconfigure with: cmake --preset release"
