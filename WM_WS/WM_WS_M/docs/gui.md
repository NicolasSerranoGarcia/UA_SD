# Monitor GUI

Configure Monitor with `-DBUILD_GUI=ON` to build the Qt6 panel. The default
`-DBUILD_GUI=OFF` runs the terminal app. Both builds share `src/WM_WS_M.cpp` as
their entry point; CMake defines `BUILD_GUI` as `1` or `0` for that executable
to select `gui::run` or `app::runTerminal`. Both builds take the
same command-line options and save diagnostics to `log/wm-ws-monitor.log`
(override with `--log-file`). Fatal errors also appear on the terminal. On Windows the
executable keeps the console subsystem (`WIN32_EXECUTABLE OFF`), including
when the Qt window is enabled.

```sh
cmake -S WM_WS/WM_WS_M -B WM_WS/WM_WS_M/build/gui -DBUILD_GUI=ON
cmake --build WM_WS/WM_WS_M/build/gui
./WM_WS/WM_WS_M/build/gui/WM_WS_M --ws-id 1 --engine-ip 127.0.0.1 --central-hostname localhost --central-port 9000
```

`wm_ws_monitor` contains the monitoring logic and has no Qt dependency.
`app::run` accepts an optional stop token and `StatusObserver`. Observers run
on the calling thread and receive snapshots of startup, resolved Central,
Engine health, health response time, and the latest startup or Engine error.
Central status-reporting diagnostics are saved to the activity log. File logging
does not depend on an observer.

`wm_ws_monitor_gui` is the Qt adapter. Its `gui::Window` displays snapshots;
`gui::run` owns the Qt event loop and monitoring worker, dispatching snapshots
to the window through queued callbacks. A different frontend can link
`wm_ws_monitor` and supply its own observer without changing the backend.

Closing the window requests a stop and keeps the event loop responsive until
the worker finishes. Retry and health-check sleeps wake immediately on stop.
An in-flight socket operation can take its configured timeout; system DNS
resolution must return before shutdown completes. Startup failures remain
visible until the window is dismissed.
