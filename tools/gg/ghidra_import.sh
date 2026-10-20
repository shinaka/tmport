#!/usr/bin/env bash
# Rebuild the Gizmos & Gadgets Ghidra project from the original ssg.exe and export it.
#
#   bash tools/gg/ghidra_import.sh            (Git Bash; reads $GG_DATA/ssg.exe, default <repo>/GadgetsAndGizmos)
#
# 1. tools/gg/vroomm.py writes the flattened image extracted/gg/ssg_flat.exe (resident load module + the 33
#    VROOMM overlays at Ghidra segments 2a80..4140, fix-ups and stub entries pre-relocated) and the set-up
#    tables in extracted/gg/ghidra_setup/.
# 2. Ghidra headless creates re/ghidra_proj/GGDos (program ssg.exe; any old project is deleted first):
#      preScripts  SetupOverlays (overlay blocks, block names, DGROUP, stub thunks, far code pointers),
#                  ApplyNames re/names/gg (named entries become functions before analysis; _Noreturn ones first)
#      analysis    Ghidra's default analysers (decompiler switch analysis off)
#      postScripts FixRealModeRefs (near branches across a 64-KB page, CS-relative references),
#                  FixSwitches (Borland switch tables), FixRealModeRefs, FindMoreFunctions enter jmp16 afterdata;
#                  headless then re-analyses what the post-scripts changed.
# 3. A second run without analysis, so that nothing re-creates what it corrects: FixRealModeRefs, ApplyNames,
#    DefineStrings (+ 1000:0264, the overlay manager's error text, which sits in the C0 code segment without a NUL).
# 4. tools/gg/ghidra_export.sh re-applies the names and writes re/gg/ghidra_export/.
# Everything written is derived from the user's file and stays in ignored folders. Logs: re/gg/ghidra_headless*.log.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GG_DATA="${GG_DATA:-$REPO/GadgetsAndGizmos}"
EXE="$GG_DATA/ssg.exe"
OUT="$REPO/extracted/gg"
SETUP="$OUT/ghidra_setup"
FLAT="$OUT/ssg_flat.exe"
IMPORT_DIR="$OUT/ghidra_import"          # a copy of the flat image named ssg.exe, so the program is "ssg.exe"
PROJ_DIR="$REPO/re/ghidra_proj"
PROJ=GGDos
NAMES="$REPO/re/names/gg"
SCRIPTS="$REPO/tools/ghidra_scripts"
LOG="$REPO/re/gg/ghidra_headless.log"
LOG2="$REPO/re/gg/ghidra_headless_finish.log"
w() { cygpath -w "$1"; }
report() { grep -E "^INFO  [A-Za-z]+\.java> " "$1" | sed -e 's/^INFO  //' -e 's/ (GhidraScript) *$//'; }

[ -f "$EXE" ] || { echo "ssg.exe not found: $EXE (set GG_DATA)" >&2; exit 2; }
python -P "$REPO/tools/gg/vroomm.py" flatten "$FLAT" "$SETUP" --exe "$EXE"
mkdir -p "$IMPORT_DIR" "$PROJ_DIR" "$(dirname "$LOG")"
cp "$FLAT" "$IMPORT_DIR/ssg.exe"
rm -rf "$PROJ_DIR/$PROJ.gpr" "$PROJ_DIR/$PROJ.rep"
rm -f "$SETUP/realmode_fixes.tsv" "$SETUP/switches.tsv"

echo "importing into $PROJ_DIR/$PROJ (log $LOG) ..."
"$REPO/tools/ghidra_headless.sh" "$(w "$PROJ_DIR")" "$PROJ" -import "$(w "$IMPORT_DIR/ssg.exe")" \
    -scriptPath "$(w "$SCRIPTS")" \
    -preScript SetupOverlays.java "$(w "$SETUP/blocks.tsv")" "$(w "$SETUP/entries.tsv")" \
        "$(w "$SETUP/overlays.tsv")" "$(w "$SETUP/farcode.tsv")" \
    -preScript ApplyNames.java "$(w "$NAMES")" \
    -postScript FixRealModeRefs.java "$(w "$SETUP/realmode_fixes.tsv")" \
    -postScript FixSwitches.java "$(w "$SETUP/switches.tsv")" \
    -postScript FixRealModeRefs.java "$(w "$SETUP/realmode_fixes.tsv")" \
    -postScript FindMoreFunctions.java enter jmp16 afterdata \
    < /dev/null > "$LOG" 2>&1 || { echo "Ghidra import failed, see $LOG" >&2; exit 1; }
grep -q "REPORT: Import succeeded" "$LOG" || { echo "Ghidra import failed, see $LOG" >&2; exit 1; }
report "$LOG"

echo "finishing without analysis (log $LOG2) ..."
"$REPO/tools/ghidra_headless.sh" "$(w "$PROJ_DIR")" "$PROJ" -process ssg.exe -noanalysis \
    -scriptPath "$(w "$SCRIPTS")" \
    -postScript FixRealModeRefs.java "$(w "$SETUP/realmode_fixes.tsv")" \
    -postScript ApplyNames.java "$(w "$NAMES")" \
    -postScript DefineStrings.java "$(w "$SETUP/strings.tsv")" 1000:0264 \
    < /dev/null > "$LOG2" 2>&1 || { echo "Ghidra finishing run failed, see $LOG2" >&2; exit 1; }
grep -q "REPORT: Save succeeded" "$LOG2" || { echo "Ghidra finishing run failed, see $LOG2" >&2; exit 1; }
report "$LOG2"

LOG="$REPO/re/gg/ghidra_headless_export.log" bash "$REPO/tools/gg/ghidra_export.sh"
