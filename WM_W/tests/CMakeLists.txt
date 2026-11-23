foreach(test_case IN ITEMS valid help environment_port cli_overrides missing_key missing_central
                          invalid_central invalid_central_port invalid_port
                          malformed_json unknown_city invalid_coordinates)
  add_test(NAME WM_W.config.${test_case}
    COMMAND "${CMAKE_COMMAND}"
      "-DEXECUTABLE=$<TARGET_FILE:WM_W>"
      "-DCONFIG=${PROJECT_SOURCE_DIR}/config/cities.example.json"
      "-DCASE=${test_case}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/config_test.cmake")
  set_tests_properties(WM_W.config.${test_case} PROPERTIES TIMEOUT 10)
endforeach()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME WM_W.weather.integration
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/weather_test.py" "$<TARGET_FILE:WM_W>")
set_tests_properties(WM_W.weather.integration PROPERTIES TIMEOUT 45)
