ARG ENGINE_IMAGE=wm-engine_tests
ARG MONITOR_IMAGE=wm-monitor_tests

FROM ${MONITOR_IMAGE} AS monitor
FROM ${ENGINE_IMAGE}

COPY --from=monitor /usr/src/water_management_app/watering_station/monitor/build/WM_WS_M /usr/local/bin/WM_WS_M
COPY tests/ ../tests/

RUN cmake -S ../tests -B process-tests \
      -DENGINE_EXECUTABLE=/usr/src/water_management_app/watering_station/engine/build/WM_WS_E \
      -DMONITOR_EXECUTABLE=/usr/local/bin/WM_WS_M \
    && cmake --build process-tests --parallel 2

CMD ["ctest", "--test-dir", "process-tests", "--output-on-failure"]
