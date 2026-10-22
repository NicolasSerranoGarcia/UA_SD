cmake_minimum_required(VERSION 3.21)

project(watering_station_engine)

include(CmakeOptions.txt)

include(cmake/boost.cmake)
include(cmake/kafka.cmake)
include(cmake/ninttp.cmake)

add_subdirectory(src)

add_executable(WM_WS_E src/WM_WS_E.cpp)
target_link_libraries(WM_WS_E PRIVATE wm_ws_engine)
set_target_properties(WM_WS_E PROPERTIES WIN32_EXECUTABLE OFF)

if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(BUILD_TESTING)
  include(cmake/test.cmake)
endif()
