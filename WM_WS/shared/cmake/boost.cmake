include_guard(DIRECTORY)

if(NOT WM_WS_BOOST_COMPONENTS)
  set(WM_WS_BOOST_COMPONENTS program_options url)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/probe.cmake")
set(boost_targets Boost::headers)
foreach(component IN LISTS WM_WS_BOOST_COMPONENTS)
  list(APPEND boost_targets Boost::${component})
endforeach()
wm_ws_probe_package(Boost VERSION 1.83 COMPONENTS ${WM_WS_BOOST_COMPONENTS}
  TARGETS ${boost_targets} RESULT boost_available)
if(boost_available)
  find_package(Boost 1.83 CONFIG REQUIRED COMPONENTS ${WM_WS_BOOST_COMPONENTS})
else()
  include(FetchContent)
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
  endif()

  message(STATUS "(WM_WS) Boost not found; fetching Boost 1.89.0")
  # Asio headers are also used by the Monitor's connection test.
  set(BOOST_INCLUDE_LIBRARIES ${WM_WS_BOOST_COMPONENTS} asio)
  set(BOOST_ENABLE_COMPATIBILITY_TARGETS ON)
  set(BOOST_SKIP_INSTALL_RULES ON)
  if("process" IN_LIST WM_WS_BOOST_COMPONENTS)
    set(BOOST_PROCESS_USE_STD_FS ON CACHE BOOL "Use std::filesystem for Boost.Process")
  endif()
  FetchContent_Declare(
    Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.89.0/boost-1.89.0-cmake.tar.xz
    URL_HASH SHA256=67acec02d0d118b5de9eb441f5fb707b3a1cdd884be00ca24b9a73c995511f74
  )
  FetchContent_MakeAvailable(Boost)
endif()
