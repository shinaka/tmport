# --- The engine: MischiefEngine (https://github.com/shinaka/MischiefEngine) ----------------------
#
# The port builds against the engine commit pinned in cmake/engine_pin.cmake
# (set(MISCHIEF_ENGINE_GIT_TAG <sha>); tools/engine_pin.py prints and updates it), unless a local
# checkout is there: MISCHIEF_ENGINE_DIR (the CMake variable, else the environment variable, which
# the port's tools/mischief_engine.py reads too), else ../MischiefEngine next to this repository, is
# used as it is (a STATUS line says which). -DMISCHIEF_ENGINE_FETCH=ON fetches the pinned commit
# anyway, and -DMISCHIEF_ENGINE_GIT_TAG=<commit> fetches another one without editing the pin.
# The engine brings SDL 3 (linked through `mischief`), ymfm, doctest (mischief_use_doctest()) and
# mischief_configure_target(); docs: the engine's README.md, "Using the engine in a port".

include(FetchContent)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/engine_pin.cmake)
set(_mischief_pin "${MISCHIEF_ENGINE_GIT_TAG}")
# A -DMISCHIEF_ENGINE_GIT_TAG=<commit> is a cache entry, which the pin's normal variable hides.
if(NOT "$CACHE{MISCHIEF_ENGINE_GIT_TAG}" STREQUAL "")
    set(MISCHIEF_ENGINE_GIT_TAG "$CACHE{MISCHIEF_ENGINE_GIT_TAG}")
endif()
set(MISCHIEF_ENGINE_DIR "" CACHE PATH
    "A MischiefEngine checkout to build against (empty: the environment variable MISCHIEF_ENGINE_DIR, else ../MischiefEngine if it exists, else the pinned commit)")
set(MISCHIEF_ENGINE_GIT_REPOSITORY "https://github.com/shinaka/MischiefEngine.git" CACHE STRING
    "The repository the pinned engine commit is fetched from")
option(MISCHIEF_ENGINE_FETCH "Fetch the pinned engine commit even when a local checkout exists" OFF)

set(_mischief_local "")
if(NOT MISCHIEF_ENGINE_FETCH)
    if(MISCHIEF_ENGINE_DIR)
        set(_mischief_local "${MISCHIEF_ENGINE_DIR}")
    elseif(NOT "$ENV{MISCHIEF_ENGINE_DIR}" STREQUAL "")
        set(_mischief_local "$ENV{MISCHIEF_ENGINE_DIR}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../MischiefEngine/CMakeLists.txt")
        set(_mischief_local "${CMAKE_CURRENT_SOURCE_DIR}/../MischiefEngine")
    endif()
endif()
FetchContent_Declare(MischiefEngine
    GIT_REPOSITORY ${MISCHIEF_ENGINE_GIT_REPOSITORY}
    GIT_TAG ${MISCHIEF_ENGINE_GIT_TAG})
if(_mischief_local)
    get_filename_component(_mischief_local "${_mischief_local}" ABSOLUTE)
    if(NOT EXISTS "${_mischief_local}/CMakeLists.txt")
        message(FATAL_ERROR "MISCHIEF_ENGINE_DIR: ${_mischief_local} is not a MischiefEngine checkout")
    endif()
    message(STATUS "MischiefEngine: local checkout ${_mischief_local} (the pin ${_mischief_pin} is not used)")
    # FetchContent's own override: the checkout is used in place of the download.
    set(FETCHCONTENT_SOURCE_DIR_MISCHIEFENGINE "${_mischief_local}")
else()
    if(MISCHIEF_ENGINE_GIT_TAG STREQUAL _mischief_pin)
        message(STATUS "MischiefEngine: pinned commit ${MISCHIEF_ENGINE_GIT_TAG} from ${MISCHIEF_ENGINE_GIT_REPOSITORY}")
    else()
        message(STATUS "MischiefEngine: commit ${MISCHIEF_ENGINE_GIT_TAG} from ${MISCHIEF_ENGINE_GIT_REPOSITORY} (MISCHIEF_ENGINE_GIT_TAG; the pin ${_mischief_pin} is not used)")
    endif()
    set(FETCHCONTENT_SOURCE_DIR_MISCHIEFENGINE "")
endif()
FetchContent_MakeAvailable(MischiefEngine)
unset(_mischief_local)
unset(_mischief_pin)
