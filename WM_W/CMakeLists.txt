cmake_minimum_required(VERSION 3.21)

project(weather_control_office LANGUAGES CXX)

include(CmakeOptions.txt)
include(cmake/boost.cmake)

add_subdirectory(src)

add_executable(WM_W src/WM_W.cpp)
target_link_libraries(WM_W PRIVATE wm_weather)

if(BUILD_TESTING)
  include(cmake/test.cmake)
endif()
