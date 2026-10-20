@echo off
rem Builds a personal, ready-to-play copy of the Gizmos & Gadgets port: dist\GGPort\ggport.exe next to
rem your game folder, so the game starts with no arguments (double-click ggport.exe). For your own use
rem only: the result contains your copy of the game. See docs\BUILDING.md, "A ready-to-play copy".
rem
rem Usage: scripts\package_gg.bat [game folder] [--out <dir>] [--zip]
rem   game folder: the one with SSG.EXE and GIZMO000.DAT. Default: GG_DATA, then the repository's
rem   GadgetsAndGizmos\.
rem Builds in build-package\ (set BUILD_DIR to change it) with the C runtime linked in, so the copy
rem also runs on PCs without the Visual C++ runtime.
setlocal

set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
if not defined BUILD_DIR set "BUILD_DIR=%REPO%\build-package"

call "%~dp0build.bat" -DTM_BUILD_TESTS=OFF -DTM_BUILD_GG=ON -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded || exit /b 1

rem build.bat keeps its environment to itself; use the CMake that configured the build.
set "CMAKE_EXE="
for /f "tokens=2 delims==" %%C in ('findstr /b /c:"CMAKE_COMMAND:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt"') do set "CMAKE_EXE=%%C"
if not defined CMAKE_EXE (
    echo package_gg.bat: CMAKE_COMMAND not found in "%BUILD_DIR%\CMakeCache.txt".
    exit /b 1
)

"%CMAKE_EXE%" -DTM_REPO="%REPO%" -DTM_BUILD_DIR="%BUILD_DIR%" -P "%REPO%\scripts\package_gg.cmake" -- %* || exit /b 1
endlocal
