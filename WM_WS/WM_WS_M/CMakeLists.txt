cmake_minimum_required(VERSION 3.21)

project(watering_station_monitor)

include(CmakeOptions.txt)

include(cmake/boost.cmake)
include(cmake/ninttp.cmake)
find_package(Threads REQUIRED)

add_subdirectory(src)

add_executable(WM_WS_M src/WM_WS_M.cpp)
target_compile_definitions(WM_WS_M PRIVATE BUILD_GUI=$<BOOL:${BUILD_GUI}>)

if(BUILD_GUI)
  include(cmake/qt.cmake)
  add_library(wm_ws_monitor_gui src/gui/window.cpp src/gui/run.cpp)
  target_link_libraries(wm_ws_monitor_gui PUBLIC wm_ws_monitor wm_ws_qt)
  set_target_properties(wm_ws_monitor_gui PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC ON)
  target_link_libraries(WM_WS_M PRIVATE wm_ws_monitor_gui)
else()
  target_link_libraries(WM_WS_M PRIVATE wm_ws_monitor)
endif()

# Keep the console subsystem in GUI builds so stdout/stderr stay available.
set_target_properties(WM_WS_M PROPERTIES WIN32_EXECUTABLE OFF)

if(BUILD_TESTING)
  include(cmake/test.cmake)
endif()
