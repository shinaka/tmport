# Assembles a personal, ready-to-play copy of TM Port: the engine next to the user's game data,
# so the game starts with no arguments. Run by scripts/package.bat (Windows) and
# scripts/package.sh (Linux/macOS) after they have built the engine; see docs/BUILDING.md,
# "A ready-to-play copy".
#
#   cmake -DTM_REPO=<repo> -DTM_BUILD_DIR=<build dir> -P scripts/package.cmake -- [options]
#
# Options (after --):
#   [--data] <path>     TMTWINCD folder (or a folder containing it), or a CD image (.cue/.iso).
#                       Default: TM_DATA, then the repository's extracted/wincd/TMTWINCD, TMTWINCD,
#                       then a single .cue/.iso under TM-WIN/.
#   --dos <dir>         Folder of the 1990 DOS edition (the one holding SST.EXE) to include as
#                       TM-DOS/. Default: TM_DATA_DOS, then SST.EXE in the game data folder, in one
#                       of its subfolders or next to it, then the repository's TM-DOS/.
#   --no-dos            Do not include the DOS edition.
#   --soundfont <sf2>   General MIDI SoundFont to include (music on Linux/macOS; ignored on Windows
#                       unless the MIDI output is set to soundfont).
#   --out <dir>         Output folder (default: <repo>/dist).
#   --zip               Also write a .zip of the result next to it.
#
# Result: <out>/TMPort/ with tmport[.exe] and TMTWINCD/ (or the CD image) on Windows and Linux;
# <out>/TMPort.app with the data in Contents/Resources on macOS (SDL_GetBasePath() returns that
# folder for an app bundle, and the engine's data auto-detection starts there). A DOS folder goes
# to TM-DOS/ beside it; with both editions the copy starts the Windows CD edition (the DOS edition
# with --edition dos) until the DOS edition is playable. Either edition alone is fine.
#
# The result contains the user's copy of the game: it is for personal use and must not be shared.

cmake_minimum_required(VERSION 3.24)

if(NOT TM_REPO OR NOT TM_BUILD_DIR)
    message(FATAL_ERROR "package.cmake: run it through scripts/package.bat or scripts/package.sh")
endif()
file(REAL_PATH "${TM_REPO}" TM_REPO)
file(REAL_PATH "${TM_BUILD_DIR}" TM_BUILD_DIR)

# --- arguments ------------------------------------------------------------------------------------
set(data "")
set(dos "")
set(no_dos OFF)
set(soundfont "")
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
    if(a STREQUAL "--data" OR a STREQUAL "--soundfont" OR a STREQUAL "--out" OR a STREQUAL "--dos")
        if(NOT i LESS CMAKE_ARGC)
            message(FATAL_ERROR "${a} needs a value")
        endif()
        set(v "${CMAKE_ARGV${i}}")
        math(EXPR i "${i} + 1")
        string(SUBSTRING "${a}" 2 -1 name)
        set(${name} "${v}")
    elseif(a STREQUAL "--zip")
        set(zip ON)
    elseif(a STREQUAL "--no-dos")
        set(no_dos ON)
    elseif(a STREQUAL "--help" OR a STREQUAL "-h")
        message("Usage: package [--data] <TMTWINCD folder | .cue | .iso> [--dos <DOS folder> | --no-dos] [--soundfont <sf2>] [--out <dir>] [--zip]")
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
# Case-insensitive lookup of an entry in a folder (the CD's names are upper case, copies often not).
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

# Sets out_var to the game folder (the one holding TMTWINCD.EXE) for <path> or <path>/TMTWINCD.
function(game_folder path out_var)
    set(${out_var} "" PARENT_SCOPE)
    find_entry("${path}" "TMTWINCD.EXE" exe)
    if(exe AND NOT IS_DIRECTORY "${exe}")
        set(${out_var} "${path}" PARENT_SCOPE)
        return()
    endif()
    find_entry("${path}" "TMTWINCD" sub)
    if(sub)
        find_entry("${sub}" "TMTWINCD.EXE" exe)
        if(exe AND NOT IS_DIRECTORY "${exe}")
            set(${out_var} "${sub}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

# Sets out_var to the first folder holding SST.EXE (the 1990 DOS edition): <path> itself, then its
# subfolders by name.
function(dos_folder path out_var)
    set(${out_var} "" PARENT_SCOPE)
    if(NOT IS_DIRECTORY "${path}")
        return()
    endif()
    find_entry("${path}" "SST.EXE" exe)
    if(exe AND NOT IS_DIRECTORY "${exe}")
        set(${out_var} "${path}" PARENT_SCOPE)
        return()
    endif()
    file(GLOB subs LIST_DIRECTORIES true "${path}/*")
    list(SORT subs)
    foreach(s IN LISTS subs)
        if(IS_DIRECTORY "${s}")
            find_entry("${s}" "SST.EXE" exe)
            if(exe AND NOT IS_DIRECTORY "${exe}")
                set(${out_var} "${s}" PARENT_SCOPE)
                return()
            endif()
        endif()
    endforeach()
endfunction()

# --- game data ------------------------------------------------------------------------------------
if(data STREQUAL "" AND DEFINED ENV{TM_DATA} AND NOT "$ENV{TM_DATA}" STREQUAL "")
    set(data "$ENV{TM_DATA}")
    message(STATUS "game data from TM_DATA")
endif()
if(data STREQUAL "")
    foreach(candidate "${TM_REPO}/extracted/wincd/TMTWINCD" "${TM_REPO}/TMTWINCD")
        game_folder("${candidate}" found)
        if(found)
            set(data "${found}")
            break()
        endif()
    endforeach()
endif()
if(data STREQUAL "")
    file(GLOB_RECURSE images "${TM_REPO}/TM-WIN/*.cue" "${TM_REPO}/TM-WIN/*.iso")
    list(LENGTH images n)
    if(n EQUAL 1)
        set(data "${images}")
    elseif(n GREATER 1)
        message(FATAL_ERROR "Several CD images under TM-WIN/; pass the one to use:\n  ${images}")
    endif()
endif()
# The 1990 DOS edition: --dos, TM_DATA_DOS, SST.EXE in / under / next to the game data, the
# repository's TM-DOS. A --data folder that is the DOS edition itself counts as --dos.
if(NOT no_dos)
    if(dos STREQUAL "" AND NOT data STREQUAL "" AND IS_DIRECTORY "${data}")
        game_folder("${data}" win_here)
        dos_folder("${data}" dos_here)
        if(dos_here AND NOT win_here)
            set(dos "${dos_here}")
            set(data "")
            message(STATUS "game data: ${dos} is the DOS edition")
        endif()
    endif()
    if(dos STREQUAL "" AND DEFINED ENV{TM_DATA_DOS} AND NOT "$ENV{TM_DATA_DOS}" STREQUAL "")
        set(dos "$ENV{TM_DATA_DOS}")
        message(STATUS "DOS edition from TM_DATA_DOS")
    endif()
    if(dos STREQUAL "" AND NOT data STREQUAL "" AND EXISTS "${data}")
        if(IS_DIRECTORY "${data}")
            dos_folder("${data}" found)
        else()
            set(found "")
        endif()
        if(NOT found)
            get_filename_component(data_parent "${data}" DIRECTORY)
            dos_folder("${data_parent}" found)
        endif()
        if(found)
            set(dos "${found}")
        endif()
    endif()
    if(dos STREQUAL "")
        dos_folder("${TM_REPO}/TM-DOS" found)
        if(found)
            set(dos "${found}")
        endif()
    endif()
    if(NOT dos STREQUAL "")
        dos_folder("${dos}" dos_dir)
        if(NOT dos_dir)
            message(FATAL_ERROR "${dos} is not the DOS edition's folder: it has no SST.EXE (nor a subfolder with one).")
        endif()
        file(REAL_PATH "${dos_dir}" dos_dir)
        message(STATUS "DOS edition: folder ${dos_dir}")
    endif()
endif()

if(data STREQUAL "" AND NOT dos_dir)
    message(FATAL_ERROR
        "No game data found. Pass the Treasure Mountain! Windows CD data: the TMTWINCD folder, "
        "or the CD image (.cue/.bin or .iso), e.g.\n  scripts/package.sh ~/Games/TMTWINCD\n"
        "(or the folder of the 1990 DOS edition with --dos <folder>)")
endif()
if(NOT data STREQUAL "" AND NOT EXISTS "${data}")
    message(FATAL_ERROR "Game data not found: ${data}")
endif()
if(NOT data STREQUAL "")
    file(REAL_PATH "${data}" data)
endif()

set(image_files "")
set(game_dir "")
if(data STREQUAL "")
    message(STATUS "no Windows CD data: the copy holds the DOS edition only")
elseif(IS_DIRECTORY "${data}")
    game_folder("${data}" game_dir)
    if(NOT game_dir)
        message(FATAL_ERROR "${data} is not the game folder: it has no TMTWINCD.EXE (nor a TMTWINCD folder with one).")
    endif()
    message(STATUS "game data: folder ${game_dir}")
else()
    get_filename_component(ext "${data}" LAST_EXT)
    string(TOLOWER "${ext}" ext)
    get_filename_component(image_dir "${data}" DIRECTORY)
    if(ext STREQUAL ".cue")
        # The .cue names its track files; copy them under the same names.
        list(APPEND image_files "${data}")
        file(STRINGS "${data}" cue_lines REGEX "^[ \t]*FILE[ \t]")
        foreach(line IN LISTS cue_lines)
            # (Two separate tests: if() evaluates both sides of an OR, and the second would
            # overwrite CMAKE_MATCH_1.)
            set(track_name "")
            if(line MATCHES "FILE[ \t]+\"([^\"]+)\"")
                set(track_name "${CMAKE_MATCH_1}")
            elseif(line MATCHES "FILE[ \t]+([^ \t]+)")
                set(track_name "${CMAKE_MATCH_1}")
            endif()
            if(NOT track_name STREQUAL "")
                set(track "${image_dir}/${track_name}")
                if(NOT EXISTS "${track}")
                    message(FATAL_ERROR "${data} refers to ${track_name}, which is not next to it.")
                endif()
                list(APPEND image_files "${track}")
            endif()
        endforeach()
    elseif(ext STREQUAL ".iso")
        list(APPEND image_files "${data}")
    else()
        message(FATAL_ERROR "${data}: expected the TMTWINCD folder, a .cue or an .iso (for a .bin, pass its .cue).")
    endif()
    message(STATUS "game data: CD image ${data}")
endif()

if(NOT soundfont STREQUAL "")
    if(NOT EXISTS "${soundfont}" OR IS_DIRECTORY "${soundfont}")
        message(FATAL_ERROR "SoundFont not found: ${soundfont}")
    endif()
    file(REAL_PATH "${soundfont}" soundfont)
endif()

# --- engine ---------------------------------------------------------------------------------------
set(exe "")
foreach(candidate tmport.exe tmport Release/tmport.exe Release/tmport)
    if(EXISTS "${TM_BUILD_DIR}/${candidate}" AND NOT IS_DIRECTORY "${TM_BUILD_DIR}/${candidate}")
        set(exe "${TM_BUILD_DIR}/${candidate}")
        break()
    endif()
endforeach()
if(exe STREQUAL "")
    message(FATAL_ERROR "No tmport executable in ${TM_BUILD_DIR}; build it first.")
endif()
get_filename_component(exe_name "${exe}" NAME)

# --- assemble -------------------------------------------------------------------------------------
if(CMAKE_HOST_APPLE)
    set(package "${out}/TMPort.app")
    set(bin_dir "${package}/Contents/MacOS")
    set(data_dir "${package}/Contents/Resources")
    set(platform macOS)
else()
    set(package "${out}/TMPort")
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
                            "Is TM Port still running from there? Close it and run the script again. "
                            "Nothing was changed.")
    endif()
endif()
file(REMOVE_RECURSE "${package}")
file(MAKE_DIRECTORY "${bin_dir}" "${data_dir}")
file(COPY "${exe}" DESTINATION "${bin_dir}")

if(image_files)
    file(COPY ${image_files} DESTINATION "${data_dir}")
elseif(game_dir)
    # Copy the folder's contents under the name the engine looks for.
    file(COPY "${game_dir}/" DESTINATION "${data_dir}/TMTWINCD")
endif()
if(dos_dir)
    # The engine finds a subfolder with SST.EXE next to the executable (--edition dos while the
    # Windows edition is there too).
    file(COPY "${dos_dir}/" DESTINATION "${data_dir}/TM-DOS")
endif()
if(NOT soundfont STREQUAL "")
    file(COPY "${soundfont}" DESTINATION "${data_dir}")
endif()

if(CMAKE_HOST_APPLE)
    file(WRITE "${package}/Contents/Info.plist" [=[<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleExecutable</key><string>tmport</string>
    <key>CFBundleIdentifier</key><string>io.github.shinaka.tmport</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>TM Port</string>
    <key>CFBundleDisplayName</key><string>Treasure Mountain! (TM Port)</string>
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

file(WRITE "${out}/TMPort-README.txt"
"TM Port - personal ready-to-play copy (${platform})

Start ${exe_name} (TMPort.app on macOS) with no arguments: the game data is next to it.
If the copy holds both editions, the Windows CD edition starts; add --edition dos for the 1990 DOS
edition (still in development).
Saves and settings live in your user profile, not here, so rebuilding this copy keeps them.

This copy contains your own copy of Super Solvers Treasure Mountain! (c) The Learning Company.
It is for your personal use only: do not share or upload it.
")

if(zip)
    get_filename_component(package_name "${package}" NAME)
    set(zip_file "${out}/TMPort-${platform}.zip")
    file(REMOVE "${zip_file}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${zip_file}" --format=zip "${package_name}" TMPort-README.txt
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
