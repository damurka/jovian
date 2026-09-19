# From https://github.com/Kitware/VTK/blob/master/CMake/FindR.cmake
#
# - This module locates an installed R distribution.
#
# Input:
#  R_LIB_ARCH - For windows (i386 or x64)
#
# Defines the following:
#  R_COMMAND           - Path to R command
#  R_SCRIPT_COMMAND    - Path to RScript command
#  R_HOME              - Path to 'R home', as reported by R
#  R_INCLUDE_DIR       - Path to R include directory
#  R_LIBRARY_BASE      - Path to R library
#  R_LIBRARY_BLAS      - Path to Rblas / blas library
#  R_LIBRARY_LAPACK    - Path to Rlapack / lapack library
#  R_LIBRARY_READLINE  - Path to readline library
#  R_LIBRARIES         - Array of: R_LIBRARY_BASE, R_LIBRARY_BLAS, R_LIBRARY_LAPACK, R_LIBRARY_BASE [, R_LIBRARY_READLINE]
#  R_LDFLAGS           - R CMD config --ldflags
#
# Variable search order:
#   1. Attempt to locate and set R_COMMAND
#     - If unsuccessful, generate error and prompt user to manually set R_COMMAND
#   2. Use R_COMMAND to set R_HOME
#   3. Locate other libraries in the priority:
#     1. Within a user-built instance of R at R_HOME
#     2. Within an installed instance of R
#     3. Within external system libraries
#

# On Windows/MSVC, link.exe cannot link directly against a .dll -- it needs
# the matching .lib import library (LNK1107 "invalid or corrupt file" is
# what you get if you try). R's own Windows distribution normally ships
# R.lib/Rblas.lib/Rlapack.lib right next to the corresponding .dll, but not
# every install does: a full interactive CRAN install does, but at least
# one common headless/unattended install path (confirmed via CI: R 4.6.1
# installed non-interactively by r-lib/actions/setup-r's
# `/VERYSILENT /SUPPRESSMSGBOXES` installer) does not. Below,
# find_library() is deliberately allowed to fall back to matching a .dll
# when no .lib exists (see its appended CMAKE_FIND_LIBRARY_SUFFIXES) --
# _r_ensure_import_library() then synthesizes a real .lib for any variable
# that fell back that way, using the standard Windows technique for
# creating an import library from a DLL with no accompanying .lib:
# dumpbin /exports to list its symbols, write them into a generated .def,
# then lib.exe /DEF: to produce the .lib. dumpbin/lib.exe always ship
# alongside cl.exe/link.exe in the same MSVC toolset bin directory, found
# here via CMAKE_CXX_COMPILER's own directory (guaranteed set once
# project(... LANGUAGES CXX) in the root CMakeLists.txt runs, which happens
# before find_package(R REQUIRED) is ever reached, regardless of which
# generator is in use -- CMAKE_LINKER's reliability specifically under the
# Visual Studio generator is less certain) rather than assuming they're on
# PATH -- they generally aren't, unless this configure happens to run from
# inside a Visual Studio Developer Command Prompt.
function(_r_ensure_import_library VAR_NAME)
  if(NOT WIN32 OR NOT MSVC)
    return()
  endif()
  set(_lib_path "${${VAR_NAME}}")
  if(NOT _lib_path OR NOT _lib_path MATCHES "\\.dll$")
    return()
  endif()

  get_filename_component(_dll_name "${_lib_path}" NAME_WE)
  set(_generated_dir "${CMAKE_BINARY_DIR}/generated_import_libs")
  set(_generated_lib "${_generated_dir}/${_dll_name}.lib")

  if(EXISTS "${_generated_lib}")
    set(${VAR_NAME} "${_generated_lib}" PARENT_SCOPE)
    return()
  endif()

  if(NOT CMAKE_CXX_COMPILER)
    message(WARNING "FindR.cmake: ${_lib_path} has no matching .lib and CMAKE_CXX_COMPILER isn't set yet -- "
                     "cannot locate dumpbin/lib.exe to generate one. Linking against ${VAR_NAME} will likely fail.")
    return()
  endif()
  # cl.exe's own directory (e.g. .../VC/Tools/MSVC/<ver>/bin/Hostx64/x64) IS
  # the MSVC toolset bin directory -- link.exe/lib.exe/dumpbin.exe are
  # always right there alongside it.
  get_filename_component(_msvc_tools_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  find_program(_r_dumpbin_exe dumpbin HINTS "${_msvc_tools_dir}")
  find_program(_r_lib_exe lib HINTS "${_msvc_tools_dir}")
  if(NOT _r_dumpbin_exe OR NOT _r_lib_exe)
    message(WARNING "FindR.cmake: ${_lib_path} has no matching .lib, and dumpbin/lib.exe "
                     "weren't found next to the compiler (${_msvc_tools_dir}) to generate one. "
                     "Linking against ${VAR_NAME} will likely fail.")
    return()
  endif()

  execute_process(
    COMMAND "${_r_dumpbin_exe}" /exports "${_lib_path}"
    OUTPUT_VARIABLE _dumpbin_output
    RESULT_VARIABLE _dumpbin_result
  )
  if(NOT _dumpbin_result EQUAL 0)
    message(WARNING "FindR.cmake: dumpbin /exports failed on ${_lib_path}; cannot generate an import library.")
    return()
  endif()

  # Each export line looks like "   <ordinal>    <hint> <RVA> <name>"
  # (whitespace-sensitive columns, but free-form width) -- match the whole
  # line first (MATCHALL only returns whole matches, not capture groups),
  # then strip the ordinal/hint/RVA prefix off each to get just the name.
  string(REGEX MATCHALL "\n *[0-9]+ +[0-9A-Fa-f]+ +[0-9A-Fa-f]+ +[A-Za-z_?@$][^ \r\n]*" _export_lines "${_dumpbin_output}")
  if(NOT _export_lines)
    message(WARNING "FindR.cmake: found no exported symbols in ${_lib_path} via dumpbin; cannot generate an import library.")
    return()
  endif()

  set(_def_content "LIBRARY \"${_dll_name}\"\nEXPORTS\n")
  foreach(_line IN LISTS _export_lines)
    string(REGEX REPLACE "^\n *[0-9]+ +[0-9A-Fa-f]+ +[0-9A-Fa-f]+ +" "" _symbol "${_line}")
    string(APPEND _def_content "${_symbol}\n")
  endforeach()

  file(MAKE_DIRECTORY "${_generated_dir}")
  set(_def_file "${_generated_dir}/${_dll_name}.def")
  file(WRITE "${_def_file}" "${_def_content}")

  execute_process(
    COMMAND "${_r_lib_exe}" "/DEF:${_def_file}" "/OUT:${_generated_lib}" "/MACHINE:X64"
    WORKING_DIRECTORY "${_generated_dir}"
    RESULT_VARIABLE _lib_result
    OUTPUT_VARIABLE _lib_output
    ERROR_VARIABLE _lib_error
  )
  if(NOT _lib_result EQUAL 0 OR NOT EXISTS "${_generated_lib}")
    message(WARNING "FindR.cmake: lib.exe /DEF: failed to generate ${_generated_lib} from ${_lib_path}: ${_lib_output} ${_lib_error}")
    return()
  endif()

  list(LENGTH _export_lines _num_symbols)
  message(STATUS "FindR.cmake: ${_lib_path} had no matching .lib -- generated ${_generated_lib} from ${_num_symbols} exported symbols.")
  set(${VAR_NAME} "${_generated_lib}" PARENT_SCOPE)
endfunction()

if(NOT R_LIB_ARCH)
  if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    set(R_LIB_ARCH x64)
  else()
    set(R_LIB_ARCH i386)
  endif()
endif()

set(TEMP_CMAKE_FIND_APPBUNDLE ${CMAKE_FIND_APPBUNDLE})
set(CMAKE_FIND_APPBUNDLE "NEVER")

# find_program(R_COMMAND R DOC "R executable.")
find_program(R_COMMAND R 
    HINTS "C:/Program Files/R/R-4.6.0/bin/x64" 
    DOC "R executable."
)
find_program(R_SCRIPT_COMMAND Rscript DOC "Rscript executable.")

set(CMAKE_FIND_APPBUNDLE ${TEMP_CMAKE_FIND_APPBUNDLE})

if(R_COMMAND)
  # temporarily append ".dll" to the cmake find_library suffixes
  set(OLD_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES} ".dll")

  if(JOVIAN_R_CROSS_COMPILING)
    # Find the pkg-config executable
    find_program(PKG_CONFIG_EXECUTABLE NAMES pkg-config)

    if (PKG_CONFIG_EXECUTABLE)
        # Get the R version using pkg-config
        execute_process(
            COMMAND env PKG_CONFIG_PATH=${CMAKE_PREFIX_PATH}/lib/pkgconfig ${PKG_CONFIG_EXECUTABLE} --modversion libR
            OUTPUT_VARIABLE R_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        # Extract the major, minor, and patch version numbers
        string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ ${R_VERSION})
        set(R_VERSION_MAJOR ${CMAKE_MATCH_1})
        set(R_VERSION_MINOR ${CMAKE_MATCH_2}.${CMAKE_MATCH_3})
    else()
        message(FATAL_ERROR "pkg-config executable not found")
    endif()
  else()
    execute_process(COMMAND ${R_SCRIPT_COMMAND} -e "cat(R.Version()$major)"
                    OUTPUT_VARIABLE R_VERSION_MAJOR
                    OUTPUT_STRIP_TRAILING_WHITESPACE)

    execute_process(COMMAND ${R_SCRIPT_COMMAND} -e "cat(R.Version()$minor)"
                    OUTPUT_VARIABLE R_VERSION_MINOR
                    OUTPUT_STRIP_TRAILING_WHITESPACE)

    set(R_VERSION_MAJOR ${R_VERSION_MAJOR} CACHE STRING "Major version of R")
    set(R_VERSION_MINOR ${R_VERSION_MINOR} CACHE STRING "Minor version of R")
  endif()

  execute_process(WORKING_DIRECTORY .
                  COMMAND ${R_COMMAND} RHOME
                  OUTPUT_VARIABLE R_ROOT_DIR
                  OUTPUT_STRIP_TRAILING_WHITESPACE)

  set(R_HOME ${R_ROOT_DIR} CACHE PATH "R home directory obtained from R RHOME")

  execute_process(WORKING_DIRECTORY .
                  COMMAND ${R_COMMAND} CMD config --ldflags
                  OUTPUT_VARIABLE R_LDFLAGS
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(R_LDFLAGS ${R_LDFLAGS} CACHE PATH "R CMD config --ldflags")

  set(R_INCLUDE_DIR "${R_HOME}/include" CACHE PATH "Path to R include directory")
  find_library(R_LIBRARY_BASE R
                HINTS ${R_ROOT_DIR}/lib ${R_ROOT_DIR}/bin/${R_LIB_ARCH}
                NO_DEFAULT_PATH
                DOC "R library (example libR.a, libR.dylib, etc.).")

  find_library(R_LIBRARY_BLAS NAMES Rblas blas
                HINTS ${R_ROOT_DIR}/lib ${R_ROOT_DIR}/bin/${R_LIB_ARCH}
                DOC "Rblas library (example libRblas.a, libRblas.dylib, etc.).")

  find_library(R_LIBRARY_LAPACK NAMES Rlapack lapack
                HINTS ${R_ROOT_DIR}/lib ${R_ROOT_DIR}/bin/${R_LIB_ARCH}
                DOC "Rlapack library (example libRlapack.a, libRlapack.dylib, etc.).")

  find_library(R_LIBRARY_READLINE readline
                DOC "(Optional) system readline library. Only required if the R libraries were built with readline support.")

  # reset cmake find_library to initial value
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_SUFFIXES})

  # As early as possible after these resolve (before anything below builds
  # R_LIBRARIES from them, and long before any target tries to link against
  # them): replace any of these that only found a bare .dll with a
  # generated .lib. See _r_ensure_import_library()'s own comment above.
  _r_ensure_import_library(R_LIBRARY_BASE)
  _r_ensure_import_library(R_LIBRARY_BLAS)
  _r_ensure_import_library(R_LIBRARY_LAPACK)


else()
  message(SEND_ERROR "FindR.cmake requires the following variables to be set: R_COMMAND")
endif()

# Note: R_LIBRARY_BASE is added to R_LIBRARIES twice; this may be due to circular linking dependencies; needs further investigation
set(R_LIBRARIES ${R_LIBRARY_BASE} ${R_LIBRARY_BLAS} ${R_LIBRARY_LAPACK} ${R_LIBRARY_BASE})
if(R_LIBRARY_READLINE)
  set(R_LIBRARIES ${R_LIBRARIES} ${R_LIBRARY_READLINE})
endif()