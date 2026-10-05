cmake_minimum_required(VERSION 3.16)

project(watering_station_monitor)

add_executable(WM_WS_M src/WM_WS_M.cpp)

target_compile_features(WM_WS_M PRIVATE cxx_std_23)

#Boost.program_options dependency for command line parsing
find_package(Boost REQUIRED COMPONENTS program_options)
target_link_libraries(WM_WS_M PRIVATE Boost::program_options)

#ninttp dependency (developed by me)
include(FetchContent)

FetchContent_Declare(
  ninttp
  GIT_REPOSITORY https://github.com/NicolasSerranoGarcia/ninttp.git
  GIT_TAG 2fc067cd48cfc479d131c44a241b09c842eafdcf #latest 
)

FetchContent_MakeAvailable(ninttp)

target_link_libraries(WM_WS_M PRIVATE ninttp::ninttp)
