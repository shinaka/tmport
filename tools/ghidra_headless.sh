#!/usr/bin/env bash
# Run Ghidra headless. Usage: ghidra_headless.sh <args...>
export JAVA_HOME='C:\OpenJDK'
export PATH="/c/OpenJDK/bin:$PATH"
exec cmd.exe //c "C:\Git\TM\tools\ghidra\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat" "$@"
