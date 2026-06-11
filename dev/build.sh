#!/usr/bin/env bash
# Schneller lokaler Scene-Dividers-Build (Ninja, ohne volles Xcode). Siehe dev/README.md.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"

cmake -S "${HERE}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "$@"
cmake --build "${BUILD_DIR}"

echo
echo "✓ scene-dividers.plugin gebaut + installiert nach:"
echo "  ~/Library/Application Support/obs-studio/plugins/scene-dividers.plugin"
echo "OBS starten und Log prüfen: [scene-dividers] Scene Dividers loaded successfully"
