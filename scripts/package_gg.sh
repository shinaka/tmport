#!/bin/sh
# Builds a personal, ready-to-play copy of the Gizmos & Gadgets port next to your game folder, so the
# game starts with no arguments: dist/GGPort/ggport on Linux, dist/GGPort.app on macOS (double-click
# it). For your own use only: the result contains your copy of the game. See docs/BUILDING.md,
# "A ready-to-play copy".
#
# Usage: scripts/package_gg.sh [game folder] [--out <dir>] [--zip]
#   game folder: the one with SSG.EXE and GIZMO000.DAT. Default: GG_DATA, then the repository's
#   GadgetsAndGizmos/.
# Builds in build-package/ (set BUILD_DIR to change it), with Ninja when it is installed.
set -e

REPO=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$REPO/build-package"}

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] && command -v ninja >/dev/null 2>&1; then
    cmake -S "$REPO" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DTM_BUILD_TESTS=OFF -DTM_BUILD_GG=ON
else
    cmake -S "$REPO" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DTM_BUILD_TESTS=OFF -DTM_BUILD_GG=ON
fi
cmake --build "$BUILD_DIR" --config Release --parallel --target ggport

cmake -DTM_REPO="$REPO" -DTM_BUILD_DIR="$BUILD_DIR" -P "$REPO/scripts/package_gg.cmake" -- "$@"
