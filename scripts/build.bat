@echo off
rem Configure and build the engine (Release, Ninja) into build\ at the repository root.
rem Usage: scripts\build.bat [extra cmake configure arguments]
rem Set BUILD_DIR to build somewhere other than build\ (e.g. one directory per parallel task).
rem Needs Visual Studio 2026 (MSVC + the bundled CMake and Ninja) and network access on the first
rem configure (SDL and doctest are fetched by CMake).
setlocal

set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"

if not defined VSROOT set "VSROOT=C:\Program Files\Microsoft Visual Studio\18\Professional"
if not exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" (
    echo build.bat: Visual Studio not found at "%VSROOT%". Set VSROOT to your installation.
    exit /b 1
)

if not defined VCToolsInstallDir (
    call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
if not defined BUILD_DIR set "BUILD_DIR=%REPO%\build"
set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

cmake -S "%REPO%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release %* || exit /b 1
cmake --build "%BUILD_DIR%" || exit /b 1

echo.
echo Built: "%BUILD_DIR%\tmport.exe"
if exist "%BUILD_DIR%\tmtests.exe" echo        "%BUILD_DIR%\tmtests.exe"
endlocal
