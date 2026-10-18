@echo off
rem Builds a personal, ready-to-play copy of TM Port: dist\TMPort\tmport.exe next to your game data,
rem so the game starts with no arguments (double-click tmport.exe). For your own use only: the result
rem contains your copy of the game. See docs\BUILDING.md, "A ready-to-play copy".
rem
rem Usage: scripts\package.bat [game data] [--soundfont <sf2>] [--out <dir>] [--zip]
rem   game data: the TMTWINCD folder or the CD image (.cue/.iso). Default: TM_DATA, then the
rem   repository's extracted\wincd\TMTWINCD, then a single .cue/.iso under TM-WIN\.
rem Builds in build-package\ (set BUILD_DIR to change it) with the C runtime linked in, so the copy
rem also runs on PCs without the Visual C++ runtime.
setlocal

set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
if not defined BUILD_DIR set "BUILD_DIR=%REPO%\build-package"

call "%~dp0build.bat" -DTM_BUILD_TESTS=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded || exit /b 1

rem build.bat keeps its environment to itself; use the CMake that configured the build.
set "CMAKE_EXE="
for /f "tokens=2 delims==" %%C in ('findstr /b /c:"CMAKE_COMMAND:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt"') do set "CMAKE_EXE=%%C"
if not defined CMAKE_EXE (
    echo package.bat: CMAKE_COMMAND not found in "%BUILD_DIR%\CMakeCache.txt".
    exit /b 1
)

"%CMAKE_EXE%" -DTM_REPO="%REPO%" -DTM_BUILD_DIR="%BUILD_DIR%" -P "%REPO%\scripts\package.cmake" -- %* || exit /b 1
endlocal
