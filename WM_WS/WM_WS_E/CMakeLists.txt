cmake_minimum_required(VERSION 3.21)

project(watering_station_engine)

include(cmake/boost.cmake)
include(cmake/kafka.cmake)
include(cmake/ninttp.cmake)

add_subdirectory(src)

add_executable(WM_WS_E src/WM_WS_E.cpp)
target_link_libraries(WM_WS_E PRIVATE wm_ws_engine)

if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
  include(cmake/test.cmake)
endif()
