include(GoogleTest)

add_executable(monitor_options_test options_test.cpp ../src/app/options.cpp)
target_compile_features(monitor_options_test PRIVATE cxx_std_23)
target_include_directories(monitor_options_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../shared"
)
target_link_libraries(monitor_options_test PRIVATE
  GTest::gtest_main Boost::program_options ninttp::ninttp
)
gtest_discover_tests(monitor_options_test PROPERTIES TIMEOUT 10)

add_executable(monitor_connect_test connect_test.cpp)
target_compile_features(monitor_connect_test PRIVATE cxx_std_23)
target_include_directories(monitor_connect_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../shared")
target_link_libraries(monitor_connect_test PRIVATE
  GTest::gtest_main Boost::headers ninttp::ninttp Threads::Threads
)
gtest_discover_tests(monitor_connect_test PROPERTIES TIMEOUT 10)

add_executable(monitor_app_test app_test.cpp)
target_link_libraries(monitor_app_test PRIVATE wm_ws_monitor GTest::gtest_main Boost::headers)
gtest_discover_tests(monitor_app_test PROPERTIES TIMEOUT 10)

if(BUILD_GUI)
  add_executable(monitor_window_test window_test.cpp)
  target_link_libraries(monitor_window_test PRIVATE wm_ws_monitor_gui GTest::gtest)
  gtest_discover_tests(monitor_window_test PROPERTIES TIMEOUT 10
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
