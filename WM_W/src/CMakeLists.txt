add_library(wm_weather app/options.cpp weather/config.cpp)
target_compile_features(wm_weather PUBLIC cxx_std_23)
target_include_directories(wm_weather PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(wm_weather PRIVATE Boost::program_options Boost::url Boost::json)
