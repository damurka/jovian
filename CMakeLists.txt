# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.24)
project ("datasuite-r" VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories - Build to dist/native/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/native/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/native/$<CONFIG>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist/native/$<CONFIG>)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")
set(DATASUITE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/native/include)
set(DATASUITE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/native/src)
set(DATASUITE_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/native/test)
set(CMAKE_VERBOSE_MAKEFILE ON)

if(NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "${CMAKE_HOST_SYSTEM_NAME}" OR CMAKE_CROSSCOMPILING)
    set(DATASUITE_R_CROSS_COMPILING ON)
else()
    set(DATASUITE_R_CROSS_COMPILING OFF)
endif()
message(STATUS "Cross-compiling: ${DATASUITE_R_CROSS_COMPILING}") 

# Versionning
# ===========

# Project version
file(STRINGS "${DATASUITE_INCLUDE_DIR}/datasuite/datasuite.hpp" datasuite_version_defines
     REGEX "inline constexpr int (major|minor|patch) = [0-9]+")
foreach(ver ${datasuite_version_defines})
    if(ver MATCHES "inline constexpr int (major|minor|patch) = ([0-9]+)")
        string(TOUPPER "${CMAKE_MATCH_1}" VER_UPPER)
        set(DATASUITE_VERSION_${VER_UPPER} "${CMAKE_MATCH_2}" CACHE INTERNAL "")
    endif()
endforeach()
set(DATASUITE_VERSION ${DATASUITE_VERSION_MAJOR}.${DATASUITE_VERSION_MINOR}.${DATASUITE_VERSION_PATCH})
message(STATUS "datasuite version: v${DATASUITE_VERSION}")

# Binary version
# See: https://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
file(STRINGS "${DATASUITE_INCLUDE_DIR}/datasuite/datasuite.hpp" datasuite_version_defines
    REGEX "inline constexpr int binary_(current|revision|age) = [0-9]+")
foreach(ver ${datasuite_version_defines})
    if(ver MATCHES "inline constexpr int binary_(current|revision|age) = ([0-9]+)")
        string(TOUPPER "${CMAKE_MATCH_1}" VER_UPPER)
        set(DATASUITE_BINARY_${VER_UPPER} "${CMAKE_MATCH_2}" CACHE INTERNAL "")
    endif()
endforeach()
set(DATASUITE_BINARY_VERSION ${DATASUITE_BINARY_CURRENT}.${DATASUITE_BINARY_REVISION}.${DATASUITE_BINARY_AGE})
message(STATUS "datasuite binary version: v${DATASUITE_BINARY_VERSION}")

# Build options
# =============

if(DATASUITE_R_CROSS_COMPILING)
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
option(DATASUITE_BUILD_SHARED_LIBS "Build datasuite shared library." ON)
option(DATASUITE_BUILD_STATIC_LIBS "Build datasuite static library (default if BUILD_SHARED_LIBS is OFF)." ON)
option(DATASUITE_STATIC_DEPENDENCIES "link statically with datasuite dependencies" OFF)

option(DATASUITE_SANITIZE_ADDRESS "Enable address sanitizer" OFF)

# Test options
option(DATASUITE_BUILD_TESTS "datasuite test suite" OFF)

# Dependencies
# ============

# nlohmann_json requires libraries that exchange json objects to be linked
# with the same version of nlohmann_json. Therefore this version should be
# the same in all DATASUITE components; to do so, downstream projects should not
# search # directly for nlohmann_json, but rely on find_dependency called by
# find_package(datasuite-r) instead.
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

message(STATUS "R_HOME           = ${R_HOME}")
message(STATUS "R_INCLUDE_DIR    = ${R_INCLUDE_DIR}")
message(STATUS "R_LDFLAGS        = ${R_LDFLAGS}")
message(STATUS "R_LIBRARY_BASE   = ${R_LIBRARY_BASE}")
message(STATUS "R_LIBRARY_BLAS   = ${R_LIBRARY_BLAS}")
message(STATUS "R_LIBRARY_LAPACK = ${R_LIBRARY_LAPACK}")
message(STATUS "R_VERSION_MAJOR  = ${R_VERSION_MAJOR}")
message(STATUS "R_VERSION_MINOR  = ${R_VERSION_MINOR}")
message(STATUS "R_COMMAND        = ${R_COMMAND}")
message(STATUS "R_SCRIPT_COMMAND = ${R_SCRIPT_COMMAND}")

add_subdirectory(native)

# Tests
include(CTest)
if(DATASUITE_BUILD_TESTS)
    set(BUILD_TESTING ON)
    message(STATUS "tests enabled")
else ()
    set(BUILD_TESTING OFF)
    message(STATUS "tests disabled") 
endif()
