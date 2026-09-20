# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.24)
project ("jovian" VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories - Build to dist/native/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/native/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/native/$<CONFIG>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/native/$<CONFIG>)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")
set(JOVIAN_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/native/include)
set(JOVIAN_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/native/src)
set(JOVIAN_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/native/test)
set(CMAKE_VERBOSE_MAKEFILE ON)

if(NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "${CMAKE_HOST_SYSTEM_NAME}" OR CMAKE_CROSSCOMPILING)
    set(JOVIAN_R_CROSS_COMPILING ON)
else()
    set(JOVIAN_R_CROSS_COMPILING OFF)
endif()
message(STATUS "Cross-compiling: ${JOVIAN_R_CROSS_COMPILING}") 

# Versionning
# ===========

# Project version
file(STRINGS "${JOVIAN_INCLUDE_DIR}/adrastea/adrastea.hpp" adrastea_version_defines
     REGEX "inline constexpr int (major|minor|patch) = [0-9]+")
foreach(ver ${adrastea_version_defines})
    if(ver MATCHES "inline constexpr int (major|minor|patch) = ([0-9]+)")
        string(TOUPPER "${CMAKE_MATCH_1}" VER_UPPER)
        set(ADRASTEA_VERSION_${VER_UPPER} "${CMAKE_MATCH_2}" CACHE INTERNAL "")
    endif()
endforeach()
set(ADRASTEA_VERSION ${ADRASTEA_VERSION_MAJOR}.${ADRASTEA_VERSION_MINOR}.${ADRASTEA_VERSION_PATCH})
message(STATUS "adrastea version: v${ADRASTEA_VERSION}")

# Binary version
# See: https://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
file(STRINGS "${JOVIAN_INCLUDE_DIR}/adrastea/adrastea.hpp" adrastea_version_defines
    REGEX "inline constexpr int binary_(current|revision|age) = [0-9]+")
foreach(ver ${adrastea_version_defines})
    if(ver MATCHES "inline constexpr int binary_(current|revision|age) = ([0-9]+)")
        string(TOUPPER "${CMAKE_MATCH_1}" VER_UPPER)
        set(ADRASTEA_BINARY_${VER_UPPER} "${CMAKE_MATCH_2}" CACHE INTERNAL "")
    endif()
endforeach()
set(ADRASTEA_BINARY_VERSION ${ADRASTEA_BINARY_CURRENT}.${ADRASTEA_BINARY_REVISION}.${ADRASTEA_BINARY_AGE})
message(STATUS "adrastea binary version: v${ADRASTEA_BINARY_VERSION}")

# Build options
# =============

if(JOVIAN_R_CROSS_COMPILING)
    # Location of the target environment
    set(CMAKE_FIND_ROOT_PATH  ${CMAKE_PREFIX_PATH})

    # Adjust the default behavior of the FIND_XXX() commands:
    # Search executable programs in the host environment
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

    # Search headers and libraries in the target environment
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
endif()

# Compilation options
option(JOVIAN_BUILD_SHARED_LIBS "Build jovian shared library." ON)
option(JOVIAN_BUILD_STATIC_LIBS "Build jovian static library (default if BUILD_SHARED_LIBS is OFF)." ON)
option(JOVIAN_STATIC_DEPENDENCIES "link statically with jovian dependencies" OFF)

option(JOVIAN_SANITIZE_ADDRESS "Enable address sanitizer" OFF)

# Test options
option(JOVIAN_BUILD_TESTS "jovian test suite" OFF)

# Dependencies
# ============

# nlohmann_json requires libraries that exchange json objects to be linked
# with the same version of nlohmann_json. Therefore this version should be
# the same in all JOVIAN components; to do so, downstream projects should not
# search # directly for nlohmann_json, but rely on find_dependency called by
# find_package(jovian) instead.
set(nlohmann_json_REQUIRED_VERSION 3.12.0)
set(cppzmq_REQUIRED_VERSION 4.8.1)
set(zeromq_REQUIRED_VERSION 4.3.2)

if (NOT TARGET nlohmann_json)
    find_package(nlohmann_json ${nlohmann_json_REQUIRED_VERSION} REQUIRED)
    message(STATUS "Found nlohmann_json ${nlohmann_json_VERSION}")
endif ()

if (NOT TARGET cppzmq AND NOT TARGET cppzmq-static)
    find_package(cppzmq ${cppzmq_REQUIRED_VERSION} REQUIRED)
    message(STATUS "Found cppzmq ${cppzmq_VERSION}")
endif ()

if (NOT TARGET libzmq AND NOT TARGET libzmq-static)
    if (WIN32)
        find_package(zeromq ${zeromq_REQUIRED_VERSION} REQUIRED)
    else ()
        find_package(zeromq ${zeromq_REQUIRED_VERSION} QUIET)

        if (NOT ZeroMQ_FOUND)
            message(STATUS "CMake libzmq package not found, trying again with pkg-config")
            find_package(PkgConfig)
            pkg_check_modules(ZeroMQ libzmq>=${zeromq_REQUIRED_VERSION} REQUIRED)
            set(ZeroMQ_VERSION ${PC_LIBZMQ_VERSION})
            find_library(ZeroMQ_LIBRARY NAMES libzmq.so libzmq.dylib libzmq.dll
                 PATHS ${PC_LIBZMQ_LIBDIR} ${PC_LIBZMQ_LIBRARY_DIRS})
            find_library(ZeroMQ_STATIC_LIBRARY NAMES libzmq-static.a libzmq.a libzmq.dll.a
                 PATHS ${PC_LIBZMQ_LIBDIR} ${PC_LIBZMQ_LIBRARY_DIRS})
            message(STATUS "STATIC_LIBRARY" {ZeroMQ_LIBRARY})
            message(STATUS "STATIC_STATIC_LIBRARY" {ZeroMQ_STATIC_LIBRARY})
         endif ()
    endif ()
endif ()

find_package(OpenSSL REQUIRED)
find_package(R REQUIRED)

# Used by the kernel supervisor (native/src/supervisor) for its REST +
# WebSocket API -- not needed by the addon or the standalone kernel
# executable, but resolved unconditionally here to match how the other
# dependencies above are already handled regardless of which targets end up
# being built (see JOVIAN_BUILD_THEMISTO in native/CMakeLists.txt).
find_package(httplib REQUIRED)
find_package(ixwebsocket REQUIRED)

message(STATUS "R_COMMAND        = ${R_COMMAND}")
message(STATUS "R_HOME           = ${R_HOME}")
message(STATUS "R_INCLUDE_DIR    = ${R_INCLUDE_DIR}")

# Tests -- must be resolved before add_subdirectory(native), since
# native/CMakeLists.txt only adds its own test/ subdirectory when
# BUILD_TESTING is already set by the time it runs.
include(CTest)
if(JOVIAN_BUILD_TESTS)
    set(BUILD_TESTING ON)
    message(STATUS "tests enabled")
else ()
    set(BUILD_TESTING OFF)
    message(STATUS "tests disabled")
endif()

add_subdirectory(native)
