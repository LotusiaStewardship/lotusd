#!/usr/bin/env bash
#
# Cross-build a single Lotus C++ component using the depends/ system.
# Driven by Docker buildx's TARGETPLATFORM environment variable so the
# same Dockerfile can produce binaries for every Linux architecture in
# the matrix (linux/amd64, linux/arm64, linux/arm/v7). linux/386 is
# unsupported because Ubuntu has dropped i386 from its base manifests
# and the cross-toolchains are mutually exclusive with multilib.
#
# Usage:
#   cross-build.sh <cmake-target>
#
#   <cmake-target> ∈ { lotusd, lotus-cli, lotus-tx,
#                      lotus-seeder, lotus-wallet, lotus-qt }
#
# Inputs:
#   TARGETPLATFORM (defaults to linux/amd64) - set by buildx.
#   JOBS           (defaults to nproc)        - parallelism for make/ninja.
#   OUT_DIR        (defaults to /opt/lotus/bin) - where to drop the binary.
#                  Set to a bind-mounted path (e.g. /src/artifacts/<x>)
#                  when invoking the script outside of buildx so the
#                  result survives the container exit.
#
# Output:
#   $OUT_DIR/<cmake-target>                   - stripped binary for the
#                                               requested target arch.

set -euo pipefail

TARGET="${1:?usage: cross-build.sh <cmake-target>}"
PLATFORM="${TARGETPLATFORM:-linux/amd64}"
JOBS="${JOBS:-$(nproc)}"

case "$PLATFORM" in
    linux/amd64)  DEP=linux64       ; TC=cmake/platforms/Linux64.cmake     ; TRIPLE=x86_64-linux-gnu      ;;
    linux/arm64)  DEP=linux-aarch64 ; TC=cmake/platforms/LinuxAArch64.cmake; TRIPLE=aarch64-linux-gnu     ;;
    linux/arm/v7) DEP=linux-arm     ; TC=cmake/platforms/LinuxARM.cmake    ; TRIPLE=arm-linux-gnueabihf   ;;
    *) echo "ERROR: unsupported TARGETPLATFORM '$PLATFORM' (supported: linux/amd64, linux/arm64, linux/arm/v7)" >&2; exit 1 ;;
esac

case "$TARGET" in
    lotus-qt) WANT_QT=ON ;;
    lotusd|lotus-cli|lotus-tx|lotus-seeder|lotus-wallet) WANT_QT=OFF ;;
    *) echo "ERROR: unsupported cmake target '$TARGET'" >&2; exit 1 ;;
esac

NO_QT=$([[ "$WANT_QT" == "ON" ]] && echo 0 || echo 1)

echo "================================================================"
echo " Lotus cross-build"
echo "   target:   $TARGET"
echo "   platform: $PLATFORM"
echo "   depends:  build-$DEP"
echo "   triple:   $TRIPLE"
echo "   qt:       $WANT_QT"
echo "   jobs:     $JOBS"
echo "================================================================"

# 1. Build the depends/ stack for the target host.
#    jemalloc is left out: it is an optional perf optimisation and the
#    upstream depends recipe doesn't track every cross host cleanly.
make -C depends -j"$JOBS" "build-${DEP}" \
    NO_QT="$NO_QT" \
    NO_UPNP=0 NO_ZMQ=0 NO_BDB=0 NO_SQLITE=0 NO_JEMALLOC=1

# 2. Configure + build the requested cmake target.
BUILD_DIR="build_${DEP}"
mkdir -p "$BUILD_DIR"
cmake -GNinja -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TC" \
    -DBASEPREFIX="$PWD/depends" \
    -DBUILD_BITCOIN_QT="$WANT_QT" \
    -DBUILD_BITCOIN_WALLET=ON \
    -DBUILD_BITCOIN_SEEDER=ON \
    -DBUILD_BITCOIN_ZMQ=ON \
    -DENABLE_UPNP_DEFAULT=OFF \
    -DUSE_JEMALLOC=OFF \
    -DSECP256K1_BUILD_OPENSSL_TESTS=OFF
ninja -C "$BUILD_DIR" -j"$JOBS" "$TARGET"

# 3. Place the binary at a stable, runtime-stage friendly location.
OUT_DIR="${OUT_DIR:-/opt/lotus/bin}"
mkdir -p "$OUT_DIR"
case "$TARGET" in
    lotusd|lotus-cli|lotus-tx) SRC="$BUILD_DIR/src/$TARGET" ;;
    lotus-seeder)              SRC="$BUILD_DIR/src/seeder/lotus-seeder" ;;
    lotus-wallet)              SRC="$BUILD_DIR/src/lotus-wallet" ;;
    lotus-qt)                  SRC="$BUILD_DIR/src/qt/lotus-qt" ;;
esac
DEST="$OUT_DIR/$(basename "$SRC")"
cp "$SRC" "$DEST"
# Best-effort cross-strip; never fail the build if strip is unavailable.
"${TRIPLE}-strip" "$DEST" 2>/dev/null \
    || strip "$DEST" 2>/dev/null \
    || true

echo "Done. Binary: $DEST"
