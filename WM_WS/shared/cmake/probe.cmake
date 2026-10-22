include_guard(DIRECTORY)

# A failed package config may create targets that cannot be removed. Probe it
# in a separate CMake project before importing a complete package into this one.
function(wm_ws_probe_package package)
  cmake_parse_arguments(PROBE "" "RESULT;VERSION" "COMPONENTS;TARGETS" ${ARGN})
  if(CMAKE_DISABLE_FIND_PACKAGE_${package})
    set(${PROBE_RESULT} FALSE PARENT_SCOPE)
    return()
  endif()

  set(probe_dir "${CMAKE_BINARY_DIR}/_deps/${package}-probe")
  file(MAKE_DIRECTORY "${probe_dir}")
  string(JOIN " " components ${PROBE_COMPONENTS})
  set(probe_source "cmake_minimum_required(VERSION 3.21)\nproject(package_probe LANGUAGES CXX)\n")
  string(APPEND probe_source "find_package(${package} ${PROBE_VERSION} CONFIG QUIET COMPONENTS ${components})\n")
  string(APPEND probe_source "if(NOT ${package}_FOUND)\n  return()\nendif()\n")
  foreach(target IN LISTS PROBE_TARGETS)
    string(APPEND probe_source "if(NOT TARGET ${target})\n  return()\nendif()\n")
  endforeach()
  string(APPEND probe_source "file(WRITE \"${probe_dir}/found\" \"${package}\")\n")
  file(WRITE "${probe_dir}/CMakeLists.txt" "${probe_source}")
  file(REMOVE "${probe_dir}/found")

  # Carry package hints and toolchain settings into the probe, but not package
  # results or imported targets. Bracket quoting preserves paths and lists.
  set(probe_cache "")
  get_cmake_property(variables VARIABLES)
  foreach(variable IN LISTS variables)
    if(variable MATCHES "^CMAKE_(PREFIX_PATH|MODULE_PATH|IGNORE_PATH|IGNORE_PREFIX_PATH|FIND_ROOT_PATH.*|FIND_USE_.*|FIND_PACKAGE_PREFER_CONFIG|DISABLE_FIND_PACKAGE_.*|TOOLCHAIN_FILE|SYSROOT.*|(C|CXX)_(COMPILER|COMPILER_ARG1|COMPILER_TARGET|COMPILER_EXTERNAL_TOOLCHAIN|FLAGS.*)|MAKE_PROGRAM|BUILD_TYPE|OSX_.*|MSVC_RUNTIME_LIBRARY|GENERATOR_INSTANCE|TRY_COMPILE_TARGET_TYPE)$"
       OR variable MATCHES "^(Boost_(DIR|ROOT|USE_.*|NO_.*|COMPILER|ARCHITECTURE)|BOOST_ROOT|BOOST_INCLUDEDIR|BOOST_LIBRARYDIR|QT_HOST_PATH|QT_ADDITIONAL_.*PREFIX_PATH)$"
       OR (variable MATCHES "_(DIR|ROOT)$" AND NOT variable MATCHES "^(CMAKE_|PROJECT_|.*_(SOURCE|BINARY)_DIR$)"))
      string(APPEND probe_cache "set(${variable} [==[${${variable}}]==] CACHE STRING \"\" FORCE)\n")
    endif()
  endforeach()
  file(WRITE "${probe_dir}/hints.cmake" "${probe_cache}")
  set(probe_args -S "${probe_dir}" -B "${probe_dir}/build"
    -G "${CMAKE_GENERATOR}" -C "${probe_dir}/hints.cmake")
  if(CMAKE_GENERATOR_PLATFORM)
    list(APPEND probe_args -A "${CMAKE_GENERATOR_PLATFORM}")
  endif()
  if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND probe_args -T "${CMAKE_GENERATOR_TOOLSET}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" ${probe_args}
    RESULT_VARIABLE result OUTPUT_FILE "${probe_dir}/probe.log" ERROR_FILE "${probe_dir}/probe.log")
  if(result STREQUAL "0" AND EXISTS "${probe_dir}/found")
    set(${PROBE_RESULT} TRUE PARENT_SCOPE)
  else()
    set(${PROBE_RESULT} FALSE PARENT_SCOPE)
  endif()
endfunction()
