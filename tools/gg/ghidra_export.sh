#!/usr/bin/env bash
# Re-apply re/names/gg to the Gizmos & Gadgets Ghidra project and export it to re/gg/ghidra_export/
# (same layout as re/dos/ghidra_export: functions.tsv, functions/SSSS_OOOO.c, decompiled.c, disassembly.asm,
# strings.tsv, segments.tsv, imports.tsv). Run after editing the names; tools/gg/ghidra_import.sh calls it.
#
#   bash tools/gg/ghidra_export.sh
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PROJ_DIR="$REPO/re/ghidra_proj"
PROJ=GGDos
NAMES="$REPO/re/names/gg"
EXPORT="$REPO/re/gg/ghidra_export"
SCRIPTS="$REPO/tools/ghidra_scripts"
LOG="${LOG:-$REPO/re/gg/ghidra_headless_export.log}"
w() { cygpath -w "$1"; }

[ -d "$PROJ_DIR/$PROJ.rep" ] || { echo "no project $PROJ_DIR/$PROJ; run tools/gg/ghidra_import.sh" >&2; exit 2; }
rm -rf "$EXPORT"
mkdir -p "$EXPORT" "$(dirname "$LOG")"
echo "exporting to $EXPORT (log $LOG) ..."
"$REPO/tools/ghidra_headless.sh" "$(w "$PROJ_DIR")" "$PROJ" -process ssg.exe -noanalysis \
    -scriptPath "$(w "$SCRIPTS")" \
    -postScript ApplyNames.java "$(w "$NAMES")" \
    -postScript ExportAll.java "$(w "$EXPORT")" \
    < /dev/null > "$LOG" 2>&1 || { echo "Ghidra export failed, see $LOG" >&2; exit 1; }
grep -q "exported .* functions total" "$LOG" || { echo "Ghidra export failed, see $LOG" >&2; exit 1; }
grep -E "^INFO  [A-Za-z]+\.java> (functions named|exported .* total)" "$LOG" | sed -e 's/^INFO  //' -e 's/ (GhidraScript) *$//'
