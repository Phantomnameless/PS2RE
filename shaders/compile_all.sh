#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="${1:-${SCRIPT_DIR}/../build/spirv}"
mkdir -p "$OUTPUT_DIR"

GLSLC="${GLSLC:-glslc}"
FLAGS="--target-env=vulkan1.3 -O"

echo "=== Compiling shaders ==="

for f in "$SCRIPT_DIR"/vertex/*.vert \
         "$SCRIPT_DIR"/fragment/*.frag \
         "$SCRIPT_DIR"/compute/*.comp; do
    name=$(basename "$f")
    echo "  $name"
    $GLSLC $FLAGS "$f" -o "$OUTPUT_DIR/${name}.spv"
done

echo "=== Done: $(ls "$OUTPUT_DIR"/*.spv | wc -l) shaders ==="