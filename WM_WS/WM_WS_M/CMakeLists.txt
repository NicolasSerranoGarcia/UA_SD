cmake_minimum_required(VERSION 3.21)

project(watering_station_monitor)

include(cmake/boost.cmake)
include(cmake/ninttp.cmake)
find_package(Threads REQUIRED)

add_subdirectory(src)

add_executable(WM_WS_M src/WM_WS_M.cpp)
target_link_libraries(WM_WS_M PRIVATE wm_ws_monitor)

option(BUILD_GUI "Build Monitor with Qt6 GUI support" OFF)
if(BUILD_GUI)
  include(cmake/qt.cmake)
  target_link_libraries(WM_WS_M PRIVATE wm_ws_qt)
  set_target_properties(WM_WS_M PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC ON)
endif()

option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
  include(cmake/test.cmake)
endif()
