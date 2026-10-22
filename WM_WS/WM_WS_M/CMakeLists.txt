cmake_minimum_required(VERSION 3.21)

project(watering_station_monitor)

include(cmake/boost.cmake)
include(cmake/ninttp.cmake)

add_executable(WM_WS_M
  src/WM_WS_M.cpp
  src/app/app.cpp
  src/app/options.cpp
  src/nap/reporter.cpp
)

target_compile_features(WM_WS_M PRIVATE cxx_std_23)

target_include_directories(WM_WS_M PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../shared"
)

target_link_libraries(WM_WS_M PRIVATE Boost::program_options ninttp::ninttp)

find_package(Threads REQUIRED)
target_link_libraries(WM_WS_M PRIVATE Threads::Threads)

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
