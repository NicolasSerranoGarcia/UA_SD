add_library(wm_weather
  app/options.cpp
  http/client.cpp
  http/server.cpp
  weather/central_client.cpp
  weather/config.cpp
  weather/openweather.cpp
  weather/service.cpp
)
target_compile_features(wm_weather PUBLIC cxx_std_23)
target_include_directories(wm_weather PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(wm_weather PRIVATE Boost::program_options Boost::url Boost::json CURL::libcurl Threads::Threads)
if(WIN32)
  target_link_libraries(wm_weather PRIVATE ws2_32 mswsock)
endif()