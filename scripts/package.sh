#!/bin/sh
# Builds a personal, ready-to-play copy of TM Port next to your game data, so the game starts with
# no arguments: dist/TMPort/tmport on Linux, dist/TMPort.app on macOS (double-click it). For your
# own use only: the result contains your copy of the game. See docs/BUILDING.md,
# "A ready-to-play copy".
#
# Usage: scripts/package.sh [game data] [--soundfont <sf2>] [--out <dir>] [--zip]
#   game data: the TMTWINCD folder or the CD image (.cue/.iso). Default: TM_DATA, then the
#   repository's extracted/wincd/TMTWINCD, then a single .cue/.iso under TM-WIN/.
#   --soundfont: a General MIDI .sf2 to include, for music on Linux and macOS.
# Builds in build-package/ (set BUILD_DIR to change it), with Ninja when it is installed.
set -e

REPO=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$REPO/build-package"}

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] && command -v ninja >/dev/null 2>&1; then
    cmake -S "$REPO" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DTM_BUILD_TESTS=OFF
else
    cmake -S "$REPO" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DTM_BUILD_TESTS=OFF
fi
cmake --build "$BUILD_DIR" --config Release --parallel

cmake -DTM_REPO="$REPO" -DTM_BUILD_DIR="$BUILD_DIR" -P "$REPO/scripts/package.cmake" -- "$@"
