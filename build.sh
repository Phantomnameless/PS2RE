#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="${1:-Release}"
BUILD_DIR="build/$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"

echo "=== PS2→ARM64 Build: ${BUILD_TYPE} ==="

# Check architecture
ARCH=$(uname -m)
if [[ "$ARCH" != "aarch64" && "$ARCH" != "arm64" ]]; then
    echo "WARNING: Not on ARM64 ($ARCH). Cross-compilation not configured."
    echo "Set PS2RE_CROSS_TOOLCHAIN for cross-compilation."
fi

cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="${PS2RE_CROSS_TOOLCHAIN:-}" \
    .

cmake --build "$BUILD_DIR" --parallel "$(nproc)"

echo "=== Build complete: $BUILD_DIR/ps2re ==="