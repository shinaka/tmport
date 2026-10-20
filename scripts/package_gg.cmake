# Assembles a personal, ready-to-play copy of the Gizmos & Gadgets port: ggport next to the user's
# game folder, so the game starts with no arguments. Run by scripts/package_gg.bat (Windows) and
# scripts/package_gg.sh (Linux/macOS) after they have built the engine; see docs/BUILDING.md,
# "A ready-to-play copy".
#
#   cmake -DTM_REPO=<repo> -DTM_BUILD_DIR=<build dir> -P scripts/package_gg.cmake -- [options]
#
# Options (after --):
#   [--data] <dir>      The game folder (the one holding SSG.EXE and GIZMO000.DAT), or a folder
#                       containing it. Default: GG_DATA, then the repository's GadgetsAndGizmos/.
#   --out <dir>         Output folder (default: <repo>/dist).
#   --zip               Also write a .zip of the result next to it.
#
# Result: <out>/GGPort/ with ggport[.exe] and GadgetsAndGizmos/ on Windows and Linux;
# <out>/GGPort.app with the game folder in Contents/Resources on macOS (SDL_GetBasePath() returns
# that folder for an app bundle, and the engine's folder detection starts there).
#
# The result contains the user's copy of the game: it is for personal use and must not be shared.

cmake_minimum_required(VERSION 3.24)

if(NOT TM_REPO OR NOT TM_BUILD_DIR)
    message(FATAL_ERROR "package_gg.cmake: run it through scripts/package_gg.bat or scripts/package_gg.sh")
endif()
file(REAL_PATH "${TM_REPO}" TM_REPO)
file(REAL_PATH "${TM_BUILD_DIR}" TM_BUILD_DIR)

# --- arguments ------------------------------------------------------------------------------------
set(data "")
set(out "${TM_REPO}/dist")
set(zip OFF)
set(i 0)
set(after_dashes OFF)
while(i LESS CMAKE_ARGC)
    set(a "${CMAKE_ARGV${i}}")
    math(EXPR i "${i} + 1")
    if(NOT after_dashes)
        if(a STREQUAL "--")
            set(after_dashes ON)
        endif()
        continue()
    endif()
    if(a STREQUAL "--data" OR a STREQUAL "--out")
        if(NOT i LESS CMAKE_ARGC)
            message(FATAL_ERROR "${a} needs a value")
        endif()
        set(v "${CMAKE_ARGV${i}}")
        math(EXPR i "${i} + 1")
        string(SUBSTRING "${a}" 2 -1 name)
        set(${name} "${v}")
    elseif(a STREQUAL "--zip")
        set(zip ON)
    elseif(a STREQUAL "--help" OR a STREQUAL "-h")
        message("Usage: package_gg [--data] <Gizmos & Gadgets folder> [--out <dir>] [--zip]")
        return()
    elseif(a MATCHES "^-")
        message(FATAL_ERROR "unknown option ${a} (try --help)")
    elseif(data STREQUAL "")
        set(data "${a}")
    else()
        message(FATAL_ERROR "unexpected argument ${a} (try --help)")
    endif()
endwhile()

# --- helpers --------------------------------------------------------------------------------------
# Case-insensitive lookup of an entry in a folder (the game's names are upper case, copies often not).
function(find_entry dir name out_var)
    set(${out_var} "" PARENT_SCOPE)
    if(NOT IS_DIRECTORY "${dir}")
        return()
    endif()
    string(TOLOWER "${name}" want)
    file(GLOB entries LIST_DIRECTORIES true "${dir}/*")
    foreach(e IN LISTS entries)
        get_filename_component(n "${e}" NAME)
        string(TOLOWER "${n}" n)
        if(n STREQUAL want)
            set(${out_var} "${e}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()

# True when <dir> holds SSG.EXE and GIZMO000.DAT (the engine's own test, GgFiles::looksLikeGgFolder).
function(is_game_folder dir out_var)
    set(${out_var} OFF PARENT_SCOPE)
    find_entry("${dir}" "SSG.EXE" exe)
    find_entry("${dir}" "GIZMO000.DAT" vol)
    if(exe AND vol AND NOT IS_DIRECTORY "${exe}" AND NOT IS_DIRECTORY "${vol}")
        set(${out_var} ON PARENT_SCOPE)
    endif()
endfunction()

# Sets out_var to the game folder for <path>: <path> itself, then its subfolders by name.
function(game_folder path out_var)
    set(${out_var} "" PARENT_SCOPE)
    if(NOT IS_DIRECTORY "${path}")
        return()
    endif()
    is_game_folder("${path}" ok)
    if(ok)
        set(${out_var} "${path}" PARENT_SCOPE)
        return()
    endif()
    file(GLOB subs LIST_DIRECTORIES true "${path}/*")
    list(SORT subs)
    foreach(s IN LISTS subs)
        if(IS_DIRECTORY "${s}")
            is_game_folder("${s}" ok)
            if(ok)
                set(${out_var} "${s}" PARENT_SCOPE)
                return()
            endif()
        endif()
    endforeach()
endfunction()

# --- game data ------------------------------------------------------------------------------------
if(data STREQUAL "" AND DEFINED ENV{GG_DATA} AND NOT "$ENV{GG_DATA}" STREQUAL "")
    set(data "$ENV{GG_DATA}")
    message(STATUS "game data from GG_DATA")
endif()
if(data STREQUAL "" AND IS_DIRECTORY "${TM_REPO}/GadgetsAndGizmos")
    set(data "${TM_REPO}/GadgetsAndGizmos")
endif()
if(data STREQUAL "")
    message(FATAL_ERROR
        "No game data found. Pass the Super Solvers: Gizmos & Gadgets! (DOS) folder, the one with "
        "SSG.EXE and GIZMO000.DAT .. GIZMO003.DAT, e.g.\n  scripts/package_gg.sh ~/Games/GIZMOS")
endif()
if(NOT IS_DIRECTORY "${data}")
    message(FATAL_ERROR "Game folder not found: ${data}")
endif()
game_folder("${data}" game_dir)
if(NOT game_dir)
    message(FATAL_ERROR "${data} is not the Gizmos & Gadgets folder: it has no SSG.EXE and GIZMO000.DAT (nor a subfolder with them).")
endif()
file(REAL_PATH "${game_dir}" game_dir)
message(STATUS "game data: folder ${game_dir}")

# --- engine ---------------------------------------------------------------------------------------
set(exe "")
foreach(candidate ggport.exe ggport Release/ggport.exe Release/ggport)
    if(EXISTS "${TM_BUILD_DIR}/${candidate}" AND NOT IS_DIRECTORY "${TM_BUILD_DIR}/${candidate}")
        set(exe "${TM_BUILD_DIR}/${candidate}")
        break()
    endif()
endforeach()
if(exe STREQUAL "")
    message(FATAL_ERROR "No ggport executable in ${TM_BUILD_DIR}; build it first (TM_BUILD_GG=ON).")
endif()
get_filename_component(exe_name "${exe}" NAME)

# --- assemble -------------------------------------------------------------------------------------
if(CMAKE_HOST_APPLE)
    set(package "${out}/GGPort.app")
    set(bin_dir "${package}/Contents/MacOS")
    set(data_dir "${package}/Contents/Resources")
    set(platform macOS)
else()
    set(package "${out}/GGPort")
    set(bin_dir "${package}")
    set(data_dir "${package}")
    if(CMAKE_HOST_WIN32)
        set(platform Windows)
    else()
        set(platform Linux)
    endif()
endif()

# A copy that is running cannot have its executable replaced: find out before deleting anything.
if(EXISTS "${bin_dir}/${exe_name}")
    file(COPY_FILE "${exe}" "${bin_dir}/${exe_name}" RESULT rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Cannot replace ${bin_dir}/${exe_name} (${rc}).\n"
                            "Is the game still running from there? Close it and run the script again. "
                            "Nothing was changed.")
    endif()
endif()
file(REMOVE_RECURSE "${package}")
file(MAKE_DIRECTORY "${bin_dir}" "${data_dir}")
file(COPY "${exe}" DESTINATION "${bin_dir}")
# Copy the folder's contents under the name the engine looks for next to the program.
file(COPY "${game_dir}/" DESTINATION "${data_dir}/GadgetsAndGizmos")

if(CMAKE_HOST_APPLE)
    file(WRITE "${package}/Contents/Info.plist" [=[<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleExecutable</key><string>ggport</string>
    <key>CFBundleIdentifier</key><string>io.github.shinaka.ggport</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>GG Port</string>
    <key>CFBundleDisplayName</key><string>Gizmos &amp; Gadgets! (GG Port)</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>0.1</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.educational-games</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
]=])
    # Re-sign the bundle ad hoc so macOS accepts the copied binary inside it (local use only).
    find_program(CODESIGN codesign)
    if(CODESIGN)
        execute_process(COMMAND "${CODESIGN}" --force --deep --sign - "${package}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
        if(NOT rc EQUAL 0)
            message(WARNING "codesign failed (${rc}); if macOS refuses to open the app, run: codesign --force --deep --sign - \"${package}\"")
        endif()
    endif()
endif()

file(WRITE "${out}/GGPort-README.txt"
"GG Port - personal ready-to-play copy (${platform})

Start ${exe_name} (GGPort.app on macOS) with no arguments: the game folder is next to it.
Players and settings live in your user profile, not here, so rebuilding this copy keeps them.
On the first start the players of the game folder's PLAYERS.DAT are imported; that file itself is
never written.

This copy contains your own copy of Super Solvers: Gizmos & Gadgets! (c) The Learning Company.
It is for your personal use only: do not share or upload it.
")

if(zip)
    get_filename_component(package_name "${package}" NAME)
    set(zip_file "${out}/GGPort-${platform}.zip")
    file(REMOVE "${zip_file}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${zip_file}" --format=zip "${package_name}" GGPort-README.txt
                    WORKING_DIRECTORY "${out}" RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "zip failed (${rc})")
    endif()
    message(STATUS "zip: ${zip_file}")
endif()

message(STATUS "Ready: ${package}")
if(CMAKE_HOST_APPLE)
    message(STATUS "Start it with: open \"${package}\"   (or double-click it in Finder)")
else()
    message(STATUS "Start it with: \"${bin_dir}/${exe_name}\"")
endif()
