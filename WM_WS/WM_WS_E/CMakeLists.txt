cmake_minimum_required(VERSION 3.16)

project(watering_station_engine)

find_package(PkgConfig REQUIRED)
pkg_check_modules(RDKAFKA REQUIRED IMPORTED_TARGET rdkafka++)

add_executable(WM_WS_E src/main.cpp)
target_include_directories(WM_WS_E PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/engine")

#librdkafka++ dependency
target_compile_features(WM_WS_E PRIVATE cxx_std_23)
target_link_libraries(WM_WS_E PRIVATE PkgConfig::RDKAFKA)

#Boost.program_options dependency for command line parsing
find_package(Boost REQUIRED COMPONENTS program_options)
target_link_libraries(WM_WS_E PRIVATE Boost::program_options)

#ninttp dependency (developed by me)
include(FetchContent)

FetchContent_Declare(
  ninttp
  GIT_REPOSITORY https://github.com/NicolasSerranoGarcia/ninttp.git
  GIT_TAG 2fc067cd48cfc479d131c44a241b09c842eafdcf #latest 
)

FetchContent_MakeAvailable(ninttp)

target_link_libraries(WM_WS_E PRIVATE ninttp::ninttp)

if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
