set(test_key "example-key-for-local-config-test")
set(environment
  --unset=OPENWEATHER_API_KEY
  --unset=CENTRAL_API_URL
  --unset=WM_W_PORT
  --unset=WM_W_CONFIG_FILE)
set(central "https://central.example.test")
set(arguments --check-config)
set(expected_port 8080)

if(CASE STREQUAL "missing_key")
  set(expected "Set OPENWEATHER_API_KEY")
elseif(NOT CASE STREQUAL "help")
  list(APPEND environment "OPENWEATHER_API_KEY=${test_key}")
endif()

if(CASE STREQUAL "missing_central")
  set(expected "Set CENTRAL_API_URL")
else()
  if(CASE STREQUAL "invalid_central" OR CASE STREQUAL "cli_overrides")
    set(central "https:///missing-host")
    set(expected "CENTRAL_API_URL must be")
  elseif(CASE STREQUAL "invalid_central_port")
    set(central "https://central.example.test:65536")
    set(expected "CENTRAL_API_URL must be")
  endif()
  list(APPEND environment "CENTRAL_API_URL=${central}")
endif()

if(CASE STREQUAL "environment_port")
  list(APPEND environment "WM_W_PORT=9090")
  set(expected_port 9090)
elseif(CASE STREQUAL "cli_overrides")
  list(APPEND environment "WM_W_PORT=not-a-number")
  list(APPEND arguments --central-api-url "https://central.example.test"
       --port 9091 --config-file "${CONFIG}")
  set(CONFIG "${CMAKE_CURRENT_BINARY_DIR}/missing-city-file.json")
  set(expected_port 9091)
elseif(CASE STREQUAL "help")
  set(arguments --help)
elseif(CASE STREQUAL "invalid_port")
  list(APPEND environment "WM_W_PORT=65536")
  set(expected "WM_W_PORT must be")
elseif(CASE STREQUAL "malformed_json")
  set(contents [[{"cities":{]])
  set(expected "valid JSON")
elseif(CASE STREQUAL "unknown_city")
  set(contents [[{"cities":{"alicante":{"latitude":38.3,"longitude":-0.4}},"stations":{"1":"missing"}}]])
  set(expected "unknown city")
elseif(CASE STREQUAL "invalid_coordinates")
  set(contents [[{"cities":{"alicante":{"latitude":91,"longitude":-0.4}},"stations":{"1":"alicante"}}]])
  set(expected "latitude")
endif()

if(DEFINED contents)
  set(CONFIG "${CMAKE_CURRENT_BINARY_DIR}/${CASE}.json")
  file(WRITE "${CONFIG}" "${contents}")
endif()
list(APPEND environment "WM_W_CONFIG_FILE=${CONFIG}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${environment} "${EXECUTABLE}" ${arguments}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

if("${output}${error}" MATCHES "${test_key}")
  message(FATAL_ERROR "WM_W printed the API key")
endif()

if(CASE STREQUAL "help")
  if(NOT result STREQUAL "0" OR NOT output MATCHES "--config-file" OR NOT output MATCHES "--central-api-url")
    message(FATAL_ERROR "Expected help without credentials (exit: ${result}): ${output}${error}")
  endif()
elseif(CASE STREQUAL "valid" OR CASE STREQUAL "environment_port" OR CASE STREQUAL "cli_overrides")
  if(NOT result STREQUAL "0" OR NOT output MATCHES "configuration is valid.*${expected_port}")
    message(FATAL_ERROR "Expected a valid configuration on port ${expected_port} (exit: ${result}): ${output}${error}")
  endif()
elseif(result STREQUAL "0" OR NOT error MATCHES "${expected}")
  message(FATAL_ERROR "Expected '${expected}' (exit: ${result}): ${output}${error}")
endif()
