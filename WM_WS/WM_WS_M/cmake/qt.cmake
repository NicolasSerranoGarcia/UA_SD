include_guard(DIRECTORY)

# Qt is an optional Monitor dependency. Keep its source build outside this project.
include("${CMAKE_CURRENT_LIST_DIR}/../../shared/cmake/probe.cmake")
wm_ws_probe_package(Qt6 COMPONENTS Core Gui Widgets
  TARGETS Qt6::Core Qt6::Gui Qt6::Widgets RESULT qt_available)
if(qt_available)
  find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)
else()
  include(FetchContent)
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
  endif()

  set(WM_WS_QT_PREFIX "${CMAKE_BINARY_DIR}/_deps/qt-install" CACHE PATH
      "Private installation directory for the Qt source fallback")
  set(WM_WS_DEPENDENCY_JOBS 2 CACHE STRING "Parallel jobs for bootstrapped dependencies")

  if(NOT EXISTS "${WM_WS_QT_PREFIX}/wm-ws-qt-ready")
    message(STATUS "(WM_WS_M) Qt6 not found; fetching and building Qt Base 6.8.3")
    FetchContent_Declare(
      wm_ws_qtbase
      URL https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz
      URL_HASH SHA256=56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80
      # Only populate sources; Qt must be configured in its own CMake project.
      SOURCE_SUBDIR wm-ws-download-only
    )
    FetchContent_MakeAvailable(wm_ws_qtbase)

    set(qt_configure_args
      -S "${wm_ws_qtbase_SOURCE_DIR}" -B "${wm_ws_qtbase_BINARY_DIR}"
      -G "${CMAKE_GENERATOR}"
      "-DCMAKE_INSTALL_PREFIX=${WM_WS_QT_PREFIX}"
      -DCMAKE_INSTALL_LIBDIR=lib
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
      -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
      -DFEATURE_sql=OFF -DFEATURE_network=OFF -DFEATURE_testlib=OFF
      -DFEATURE_dbus=OFF -DFEATURE_printsupport=OFF
      -DFEATURE_opengl=OFF -DFEATURE_dynamicgl=OFF
    )
    if(CMAKE_TOOLCHAIN_FILE)
      list(APPEND qt_configure_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    endif()
    if(CMAKE_GENERATOR_PLATFORM)
      list(APPEND qt_configure_args -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
      list(APPEND qt_configure_args -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()
    if(CMAKE_OSX_ARCHITECTURES)
      list(APPEND qt_configure_args "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
    endif()

    # Preserve full logs without flooding the parent configuration output.
    function(wm_ws_qt_step step)
      set(log "${CMAKE_BINARY_DIR}/_deps/qt-${step}.log")
      message(STATUS "(WM_WS_M) Qt ${step}; log: ${log}")
      execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_FILE "${log}" ERROR_FILE "${log}")
      if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Qt ${step} failed (${result}). See ${log}")
      endif()
    endfunction()

    wm_ws_qt_step(configure "${CMAKE_COMMAND}" ${qt_configure_args})
    wm_ws_qt_step(build "${CMAKE_COMMAND}" --build "${wm_ws_qtbase_BINARY_DIR}"
      --config Release --parallel "${WM_WS_DEPENDENCY_JOBS}")
    wm_ws_qt_step(install "${CMAKE_COMMAND}" --install "${wm_ws_qtbase_BINARY_DIR}" --config Release)
    file(WRITE "${WM_WS_QT_PREFIX}/wm-ws-qt-ready" "Qt Base 6.8.3\n")
  endif()

  # Ignore cached component paths from an incomplete system installation.
  file(GLOB qt_package_configs "${WM_WS_QT_PREFIX}/lib/cmake/Qt6*/*Config.cmake")
  foreach(qt_package_config IN LISTS qt_package_configs)
    get_filename_component(qt_package_dir "${qt_package_config}" DIRECTORY)
    get_filename_component(qt_package "${qt_package_config}" NAME_WE)
    string(REGEX REPLACE "Config$" "" qt_package "${qt_package}")
    set(${qt_package}_DIR "${qt_package_dir}")
    unset(${qt_package}_FOUND)
  endforeach()
  list(PREPEND CMAKE_PREFIX_PATH "${WM_WS_QT_PREFIX}")
  set(Qt6_DIR "${WM_WS_QT_PREFIX}/lib/cmake/Qt6")
  find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui Widgets)
endif()

add_library(wm_ws_qt INTERFACE)
target_link_libraries(wm_ws_qt INTERFACE Qt6::Core Qt6::Gui Qt6::Widgets)

foreach(component IN ITEMS Core Gui Widgets)
  get_target_property(qt_include_dirs Qt6::${component} INTERFACE_INCLUDE_DIRECTORIES)
  get_target_property(qt_library Qt6::${component} LOCATION)
  message(STATUS "(WM_WS_M) Qt6 ${component} include dirs: ${qt_include_dirs}")
  message(STATUS "(WM_WS_M) Qt6 ${component} library: ${qt_library}")
endforeach()
