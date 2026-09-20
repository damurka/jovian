# From https://github.com/Kitware/VTK/blob/master/CMake/FindR.cmake,
# heavily trimmed -- see the comment below for why this repo only ever
# needs R's headers at build time now, nothing else.
#
# - This module locates an installed R distribution.
#
# Defines the following:
#  R_COMMAND        - Path to R command
#  R_HOME           - Path to 'R home', as reported by `R RHOME`
#  R_INCLUDE_DIR    - Path to R include directory
#
# Variable search order:
#   1. Attempt to locate and set R_COMMAND
#     - If unsuccessful, generate error and prompt user to manually set R_COMMAND
#   2. Use R_COMMAND to set R_HOME, then R_INCLUDE_DIR
#

# elara loads R's shared library dynamically at runtime on every platform
# (Windows: LoadLibrary/GetProcAddress on R.dll; Linux/macOS: dlopen/dlsym
# on libR.so/libR.dylib -- see native/src/elara/r/r_dynlib.hpp/.cpp) instead of
# linking against it at build time. This decouples elara from any specific
# R version at build time
# (switching R installations is a runtime R_HOME decision, no rebuild
# needed) and means a missing/incompatible R installation surfaces as a
# catchable error inside elara.exe rather than the OS process loader
# refusing to start it at all -- see r_dynlib.hpp's file comment for the
# full rationale.
#
# Nothing in this repo (native/CMakeLists.txt, native/test/CMakeLists.txt)
# links against an R library anymore, so this file no longer resolves any
# of that: no R_LIBRARY_BASE/BLAS/LAPACK/READLINE, R_LIBRARIES, R_LDFLAGS,
# R_VERSION_MAJOR/MINOR, R_SCRIPT_COMMAND, R_LIB_ARCH, or the
# cross-compiling pkg-config path that used to feed some of them -- all
# were either already unused outside this file or, for the libraries,
# relevant only to the generated-import-library workaround below, which
# the switch to dynamic loading made unnecessary too.
#
# That workaround handled a historical Windows-only problem: MSVC's
# link.exe can't link directly against a bare .dll (LNK1107 "invalid or
# corrupt file"), and at least one common headless R install
# (r-lib/actions/setup-r's unattended installer) doesn't ship
# R.lib/Rblas.lib/Rlapack.lib next to the .dll. It no longer exists (or is
# needed) at all now that nothing here links against an R library in the
# first place.

set(TEMP_CMAKE_FIND_APPBUNDLE ${CMAKE_FIND_APPBUNDLE})
set(CMAKE_FIND_APPBUNDLE "NEVER")

find_program(R_COMMAND R
    HINTS "C:/Program Files/R/R-4.6.0/bin/x64"
    DOC "R executable."
)

set(CMAKE_FIND_APPBUNDLE ${TEMP_CMAKE_FIND_APPBUNDLE})

if(R_COMMAND)
  execute_process(WORKING_DIRECTORY .
                  COMMAND ${R_COMMAND} RHOME
                  OUTPUT_VARIABLE R_ROOT_DIR
                  OUTPUT_STRIP_TRAILING_WHITESPACE)

  set(R_HOME ${R_ROOT_DIR} CACHE PATH "R home directory obtained from R RHOME")
  set(R_INCLUDE_DIR "${R_HOME}/include" CACHE PATH "Path to R include directory")
else()
  message(SEND_ERROR "FindR.cmake requires the following variables to be set: R_COMMAND")
endif()
