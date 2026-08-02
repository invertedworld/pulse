#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

CLEAN_FIRST=false
if [[ "${1:-}" == "--clean" ]]; then
    CLEAN_FIRST=true
fi

echo "=== Building Pulse ==="

# Configure with Release and universal arch on macOS (reconfigure if differs)
NEED_CONFIG=false
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    NEED_CONFIG=true
elif ! grep -q 'CMAKE_BUILD_TYPE:STRING=Release' "$BUILD_DIR/CMakeCache.txt"; then
    NEED_CONFIG=true
elif [[ "$(uname -s)" == Darwin ]] && ! grep -q 'CMAKE_OSX_ARCHITECTURES:STRING=arm64;x86_64' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
    NEED_CONFIG=true
fi
if [[ "$NEED_CONFIG" == true ]]; then
    CMAKE_ARGS=(-S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release)
    [[ "$(uname -s)" == Darwin ]] && CMAKE_ARGS+=(-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64")
    cmake "${CMAKE_ARGS[@]}"
fi

[[ "$CLEAN_FIRST" == true ]] && cmake --build "$BUILD_DIR" --target clean
cmake --build "$BUILD_DIR" --config Release

# Deploy to user folders (sudo not required).
# Note: "~" does not expand inside double quotes — use $HOME.
echo "=== Deploying ==="
USER_AU="$HOME/Library/Audio/Plug-Ins/Components"
USER_VST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$USER_AU" "$USER_VST"
rm -rf "$USER_AU/Pulse.component"
cp -R "$BUILD_DIR/Pulse_artefacts/Release/AU/Pulse.component" "$USER_AU/"
rm -rf "$USER_VST/Pulse.vst3"
cp -R "$BUILD_DIR/Pulse_artefacts/Release/VST3/Pulse.vst3" "$USER_VST/"

echo "=== Done — Pulse deployed ==="
