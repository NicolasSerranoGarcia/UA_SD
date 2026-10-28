# Building the watering station

Use CMake 3.21 or newer and a C++23 compiler. Git and network access are
needed when a dependency is not installed or already cached. On Windows,
use dependencies built for the same toolchain as the application (for example,
MSYS2 MinGW64), and put that toolchain's `bin` directory on `PATH`.

Run all commands from the repository root (`UA_SD`). The command examples use
Bash (Linux, WSL, or Git Bash), except where marked as PowerShell. Docker commands
require Docker running with Linux containers and Internet access during builds.

Manual tests use Kafka at `iriguchi.proxy.rlwy.net:37040` and Central NAP at
`altaria.proxy.rlwy.net:40931`. Both services must be available. The monitor
registers and activates its station on Central; use a different station ID for
each independent station being tested.

## Monitor and Engine

```sh
cmake -S WM_WS/WM_WS_M -B WM_WS/WM_WS_M/build
cmake --build WM_WS/WM_WS_M/build
ctest --test-dir WM_WS/WM_WS_M/build --output-on-failure

cmake -S WM_WS/WM_WS_E -B WM_WS/WM_WS_E/build
cmake --build WM_WS/WM_WS_E/build
ctest --test-dir WM_WS/WM_WS_E/build --output-on-failure
```

Set `-DBUILD_TESTING=OFF` to omit unit tests and GoogleTest.

Run the engine in one terminal:

```sh
./WM_WS/WM_WS_E/build/WM_WS_E --queue iriguchi.proxy.rlwy.net:37040 --hcp-port 8080 --hcp-timeout-ms 2000
```

Run the monitor in another terminal on the same host:

```sh
./WM_WS/WM_WS_M/build/WM_WS_M --ws-id 1 --engine-ip 127.0.0.1 --engine-port 8080 --central-hostname altaria.proxy.rlwy.net --central-port 40931 --monitor-location spain
```

On Windows, add `.exe` to executable paths; multi-configuration generators may
also place executables in a configuration directory such as `Debug`.
The engine's optional `BUILD_EXAMPLES` CMake option defaults to `OFF`.

### Terminal menus, logs, and recovery data

Both terminal applications show their status and all available menu options.
Choose a letter or number and press Enter. Engine supports `start`, `stop`,
`fault`, `repair`, `status`, `help`, and `quit`; Monitor supports `status`,
`help`, and `quit`. Engine updates its water measurements every second while
watering; interactive terminals update the measurement row without repeating the
menu. Closing stdin leaves the services running. Quitting Monitor
disconnects it from Engine; Engine then stops watering and waits for activation.

The simulated flow varies smoothly within 2% below the valve's configured flow.
Terminal readings and Kafka telemetry use the same measurements; delivered water
integrates the varying flow and stops accumulating when irrigation stops.

Routine diagnostics are saved to files instead of the terminal. Serious errors
remain visible on screen with technical details and a suggested next step.
Native runs use these paths relative to the current working directory:

| Data                 | Default path                                                    |
| -------------------- | --------------------------------------------------------------- |
| Engine activity log  | `log/wm-ws-engine.log`                                        |
| Monitor activity log | `log/wm-ws-monitor.log`                                       |
| Engine recovery data | `state/wm-ws-state.jsonl.0` and `state/wm-ws-state.jsonl.1` |

Directories are created automatically. Logs append across restarts and include
UTC timestamps. Override the paths with `--log-file` and Engine's `--state-file`.
Use separate recovery paths for independent stations. When upgrading an existing
native deployment, move both old `wm-ws-state.jsonl.*` files into `state/` while
Engine is stopped, or pass the old base path explicitly with `--state-file`.

The CMake dependency modules look for installed packages first. Missing
libraries are fetched at pinned versions under the build directory: Boost
1.89.0, librdkafka 2.11.1 (Engine only), ninttp at the pinned commit, and
GoogleTest 1.16.0 (tests only). Archive downloads include SHA-256 checksums.
Source builds supply the same library targets as installed packages; no
system-wide installation is performed.

librdkafka is found through its CMake package or pkg-config before falling
back to source. The fallback defaults to static libraries to avoid deploying
additional librdkafka DLLs. Its upstream build detects optional compression and security
libraries; the available features depend on the development packages installed.
The compiler, CMake, Git, platform SDKs, and operating system libraries must
still be supplied by the build environment.

## Central and Monitor outage behavior

Monitor registers and retries transient Central failures in a separate worker,
so Engine health checks continue even when Central is offline. It re-resolves
Central when opening a new connection. Rejected protocol exchanges remain visible
as controlled errors; connectivity failures retry automatically.

When Central becomes unavailable, Monitor sends the WS-local HCP `DC` command.
Engine lets an ongoing irrigation finish, keeps its measurements and pending
Kafka records, and rejects new manual or external irrigation starts. After Central
acknowledges fresh activation and reporting, Monitor sends `AC` again. An existing
administrative BLOCK still requires an external `ACTIVATE` on `central-ops`.

Monitor loss is different: Engine closes the valve when it detects HCP EOF/timeout,
revokes activation, and stops publishing to Kafka. Restarting Monitor restores
the normal activation exchange; it never resumes old irrigation.

## Optional Qt support

The Monitor's `BUILD_GUI` option defaults to `OFF`. With it disabled, Qt is
not searched for, downloaded, built, or linked. Enable the dependency support
with:

```sh
cmake -S WM_WS/WM_WS_M -B WM_WS/WM_WS_M/build/gui -DBUILD_GUI=ON
cmake --build WM_WS/WM_WS_M/build/gui
```

`cmake/qt.cmake` defines the `wm_ws_qt` interface library, linking Qt6 Core,
Gui, and Widgets. With `BUILD_GUI=ON`, Monitor runs its live Qt panel instead of
the terminal menu. The panel includes Help (F1), Open Activity Log (Ctrl+L), and
Quit Monitor (Ctrl+Q). See [the GUI guide](WM_WS_M/docs/gui.md) for controls,
status colors, and shutdown behavior. Engine retains its terminal interface.

Use `-DCMAKE_PREFIX_PATH=/path/to/Qt` to select an existing Qt installation.
If Qt is missing, Qt Base 6.8.3 is downloaded, configured, built, and installed
into `build/_deps/qt-install` during CMake configuration. This first configure
can take a long time. Subsequent configurations reuse the private installation.
Qt bootstrap logs are written to `build/_deps/qt-{configure,build,install}.log`.
The source fallback builds shared libraries for Core, Gui, and Widgets and
omits unused modules, examples, and tests.

`WM_WS_QT_PREFIX` changes the private installation path and
`WM_WS_DEPENDENCY_JOBS` controls the bootstrap's parallel build jobs (default
2). Use a separate build directory when changing compilers or Qt build settings.
Qt source builds also require the platform's development SDK and, on Linux,
the development packages for the desired display backend. See the
[Qt platform build requirements](https://doc.qt.io/qt-6.8/build-sources.html).
Deploy Qt shared libraries and platform plugins when distributing the GUI build.

## Docker: two applications in two terminals

Engine and Monitor each run in their own container, with their own interactive
terminal and independent start/stop controls. They communicate over a shared
Docker bridge network; they do not share a network namespace or need published
host ports.

Choose one of the two workflows below. Both use the container names `wm-engine`
and `wm-monitor`. Start Engine first and wait for its menu before starting
Monitor. Keep terminal 1 open while using Monitor in terminal 2. Run the commands
from the repository root in both terminals.

### Option 1: build and run the images directly

Build both images and create the network and persistent storage once:

```sh
docker build -f WM_WS/WM_WS_E/Dockerfile -t wm-engine WM_WS
docker build -f WM_WS/WM_WS_M/Dockerfile -t wm-monitor WM_WS

docker network create wm-network
docker volume create wm-engine-state
docker volume create wm-station-logs
```

Skip network creation if `wm-network` already exists. Repeating volume creation
keeps existing data. The `WM_WS` build context includes the shared headers and
CMake modules.

In **terminal 1**, start Engine with its interactive menu:

```sh
docker run -it --name wm-engine --network wm-network -v wm-engine-state:/var/lib/wm-ws -v wm-station-logs:/var/log/wm-ws wm-engine
```

In **terminal 2**, start Monitor using Engine's Docker network name (the same
command works in Bash and PowerShell):

```sh
docker run -it --name wm-monitor --network wm-network -v wm-station-logs:/var/log/wm-ws -e WS_ID=1 -e ENGINE_HOSTNAME=wm-engine wm-monitor
```

Monitor resolves Engine's hostname again on each connection attempt. Either
application can start first; Monitor retries while Engine is unavailable and
reconnects after Engine is recreated. For a fixed address, use `ENGINE_IP` instead
and leave `ENGINE_HOSTNAME` unset. The image's `127.0.0.1` IP default refers to
Monitor's own container.

### Option 2: use Compose to launch each application separately

Compose can build the images and supply the network, volumes, and environment
settings from `WM_WS/compose.yaml`. Use `run` in each terminal to launch a single
container in the foreground. Each command starts only the selected application.
There is no automatic startup or restart dependency between them.

In **terminal 1**, build and launch Engine:

```sh
docker compose -f WM_WS/compose.yaml -p ws-1 run --build --remove-orphans --no-deps --use-aliases --name wm-engine engine
```

In **terminal 2**, build and launch Monitor:

```sh
docker compose -f WM_WS/compose.yaml -p ws-1 run --build --remove-orphans --no-deps --name wm-monitor monitor
```

Use the same project name (`ws-1`) in both terminals so they join the same network.
`--use-aliases` makes Engine reachable as `engine`; Monitor's startup command
resolves that name on each reconnect. `--no-deps` ensures
a command does not launch the other application. See the
[Compose run reference](https://docs.docker.com/reference/cli/docker/compose/run/).
Omit `--build` on later launches to reuse existing images.

Compose accepts `WS_ID`, `QUEUE_ENDPOINT`, `CENTRAL_HOSTNAME`, and `CENTRAL_PORT`
from the shell or an explicit `--env-file`. To run station 2, add `-e WS_ID=2`
before `monitor` in its command. For multiple simultaneous stations, use a
different project name in both terminals and unique container names for each
pair. Each project has its own network, `engine-state` volume, and `station-logs`
volume. Choose one workflow for a station; the direct and Compose workflows use
different volume names.

### Stop, restart, or leave a terminal

Both workflows use the same controls:

- Type `quit` and press Enter to stop the application in that terminal.
- Press `Ctrl+P`, then `Ctrl+Q` to detach and leave the container running.
- Run `docker attach wm-engine` or `docker attach wm-monitor` to reconnect to a
  running container. Type `status` and press Enter to show its menu again.
- Restart exited containers with the following commands, one per terminal.

```sh
# Terminal 1
docker start -ai wm-engine
```

```sh
# Terminal 2
docker start -ai wm-monitor
```

Restarting reuses the container's original configuration. To rebuild or change
environment settings, stop and remove that container before repeating its launch
command. Persistent volumes retain logs and recovery data. After recreating
Engine, Monitor reconnects automatically when configured with its hostname. With
a fixed `ENGINE_IP`, recreate Monitor with the updated IP if that address changes.

### Logs and recovery data

Menus and serious errors appear in each application's terminal. Routine activity
is written to files in the mounted volumes:

| Data | Container path |
| ---- | -------------- |
| Engine log | `/var/log/wm-ws/wm-ws-engine.log` |
| Monitor log | `/var/log/wm-ws/wm-ws-monitor.log` |
| Engine recovery data | `/var/lib/wm-ws/state.jsonl.0` and `.1` |

From another terminal, follow either activity log while its container is running:

```sh
docker exec wm-engine tail -f /var/log/wm-ws/wm-ws-engine.log
docker exec wm-monitor tail -f /var/log/wm-ws/wm-ws-monitor.log
```

Run one command at a time, or use separate terminals. `Ctrl+C` stops following the
log without stopping the application. `docker logs` shows captured terminal
output, including menus and fatal errors.

### Test faults and recovery

Use Engine's menu to simulate a fault with `fault` and repair it with `repair`.
Monitor should show the fault and report it to Central. After repair, Monitor
requests a fresh NAP activation from Central before sending HCP `AC` to Engine.
Engine stays idle until you start watering again.

Use `start` and `stop` for manual irrigation. Manual starts publish `ws-start-conf`
with `station_id` and `operation_code: null`; Central assigns the operation code
internally. A Central `BLOCK` stops irrigation and prevents manual starts. Only
an external `ACTIVATE` clears that block; it does not resume the old irrigation.

To test Monitor loss, type `quit` in its terminal. Engine must stop watering,
reject starts, and withhold Kafka output. Run `docker start -ai wm-monitor` in the
Monitor terminal and wait for activation before starting irrigation again.

To test Engine loss, type `quit` in its terminal. Let Monitor detect the lost
connection and report it to Central, then run `docker start -ai wm-engine` in the
Engine terminal. Monitor should reconnect automatically.

A log entry such as `status acknowledged: state=fault reason=connect: ...
Connection refused` means Central accepted Monitor's report that Engine could
not be reached. The diagnostic describes the Engine connection; the NAP report
carries `KO`. Failures to report to Central are logged as `status send failed`
or `status acknowledgement failed`.

## Executable parameters

These are command-line parameters for the executables themselves. CLI defaults
are separate from the Docker defaults listed later. Ports must be in `1..65535`;
all timeout, interval, and retry values must be positive milliseconds.

### Engine: WM_WS_E

| Parameter               | CLI default                 | Purpose                                                                                         |
| ----------------------- | --------------------------- | ----------------------------------------------------------------------------------------------- |
| `--help`              | â€”                      | Print available options and exit.                                                               |
| `--queue HOST:PORT`   | Required                    | Kafka bootstrap endpoint; must be nonempty.                                                     |
| `--hcp-port PORT`     | `8080`                    | Local TCP port for monitor health connections, listening on all IPv4 interfaces.                |
| `--hcp-timeout-ms MS` | `2000`                    | HCP receive/send deadline.                                                                      |
| `--state-file PATH`   | `state/wm-ws-state.jsonl` | Base path for two synced checkpoint files (`.0` and `.1`); use a separate path per station. |
| `--log-file PATH`     | `log/wm-ws-engine.log`    | Append activity logs here; create parent directories as needed.                                 |

Native example with every operational parameter (run from the repository root):

```sh
./WM_WS/WM_WS_E/build/WM_WS_E --queue iriguchi.proxy.rlwy.net:37040 --hcp-port 8080 --hcp-timeout-ms 2000 --state-file state/wm-ws-state.jsonl --log-file log/wm-ws-engine.log
```

### Monitor: WM_WS_M

| Parameter                       | CLI default               | Purpose                                                                    |
| ------------------------------- | ------------------------- | -------------------------------------------------------------------------- |
| `--help`                      | â€”                    | Print available options and exit.                                          |
| `--ws-id ID`                  | Required                  | Station ID: 1â€“64 letters, digits, hyphens, or underscores.            |
| `--engine-ip IPV4` | Alternative | Fixed Engine IPv4 address; supply this or `--engine-hostname`. |
| `--engine-hostname HOST` | Alternative | Resolve Engine again on reconnect; mutually exclusive with `--engine-ip`. |
| `--engine-port PORT`          | `8080`                  | Engine HCP listening port.                                                 |
| `--hcp-timeout-ms MS`         | `2000`                  | HCP connect/send/response deadline.                                        |
| `--hcp-retry-ms MS`           | `1000`                  | Delay between engine connection attempts.                                  |
| `--nap-interval-ms MS`        | `1000`                  | Interval for healthy reports and retries of unacknowledged fault reports.  |
| `--nap-timeout-ms MS`         | `2000`                  | NAP connect/send/response deadline.                                        |
| `--central-hostname HOST`     | Required                  | Nonempty Central hostname; resolved to IPv4.                               |
| `--central-port PORT`         | Required                  | Central NAP TCP port.                                                      |
| `--monitor-location LOCATION` | `spain`                 | Registration location; nonempty and must not contain `#`.                |
| `--log-file PATH`             | `log/wm-ws-monitor.log` | Append activity logs here; create parent directories as needed.            |

Native example with every operational parameter, with Engine on the same host:

```sh
./WM_WS/WM_WS_M/build/WM_WS_M --ws-id 1 --engine-ip 127.0.0.1 --engine-port 8080 --hcp-timeout-ms 2000 --hcp-retry-ms 1000 --nap-interval-ms 1000 --nap-timeout-ms 2000 --central-hostname altaria.proxy.rlwy.net --central-port 40931 --monitor-location spain --log-file log/wm-ws-monitor.log
```

Print executable help inside running containers from either Docker workflow:

```sh
docker exec wm-engine ./build/WM_WS_E --help
docker exec wm-monitor ./build/WM_WS_M --help
```

An explicit executable command after the image name replaces the entire Docker
default command. Supply all required CLI arguments in that case; environment
variables are translated into CLI arguments only by the default Docker command.

## Docker parameters

Each connection parameter below is both a build argument (`--build-arg`) and an
environment variable (`docker run -e`). A build argument changes the image's
default; a runtime environment variable overrides that default for one container.

| Image          | Parameter            | Default                              | Executable argument    |
| -------------- | -------------------- | ------------------------------------ | ---------------------- |
| `wm-engine`  | `QUEUE_ENDPOINT`   | `iriguchi.proxy.rlwy.net:37040`    | `--queue`            |
| `wm-engine`  | `STATE_FILE`       | `/var/lib/wm-ws/state.jsonl`       | `--state-file`       |
| `wm-engine`  | `LOG_FILE`         | `/var/log/wm-ws/wm-ws-engine.log`  | `--log-file`         |
| `wm-monitor` | `LOG_FILE`         | `/var/log/wm-ws/wm-ws-monitor.log` | `--log-file`         |
| `wm-monitor` | `WS_ID`            | `1`                                | `--ws-id`            |
| `wm-monitor` | `ENGINE_HOSTNAME` | Empty | `--engine-hostname` (when nonempty; takes precedence over `ENGINE_IP`) |
| `wm-monitor` | `ENGINE_IP`        | `127.0.0.1`                        | `--engine-ip`        |
| `wm-monitor` | `ENGINE_PORT`      | `8080`                             | `--engine-port`      |
| `wm-monitor` | `CENTRAL_HOSTNAME` | `altaria.proxy.rlwy.net`           | `--central-hostname` |
| `wm-monitor` | `CENTRAL_PORT`     | `40931`                            | `--central-port`     |

The remaining CLI options have no Docker environment-variable mapping. To change
them, use an explicit executable command as shown above.

The named volume in the run example retains pending confirmations and final water
totals when the container is recreated. Keep the same volume for recovery of that
station; give independent stations separate volumes. Kafka IDs must be numeric
to match Central/FO and modulo routing; legacy alphanumeric HCP IDs support health
checks only. WS consumes only its `station_id % partition_count` partition of
`central-ops` and still filters station IDs to handle modulo collisions.

WS-produced telemetry uses `operator_id % partition_count` when there is an owning
FO. Without an FO, it uses the station ID for routing; Central reads all partitions.
Confirmations and final summaries use the station ID. Wire fields match
[the repository README](../Readme.md); checkpoint metadata is local only.

Central should retry an operation with the same `operation_code` until it receives
its confirmation. WS persists operation intent before valve changes and reuses
cached results for retries. First-time consumer groups start at the latest offset;
subsequent starts use committed offsets. Do not change partition counts during a
running evaluation; producers and consumers must use the same counts.

Delivery of pending records is at least once. The README's final-summary format has
no session/operation identifier, so identical summaries cannot always be distinguished
from retransmissions after a crash. Central must account for that contract limitation.
Engine samples progress at a nominal 100 ms cadence and exposes new measurements
only after saving the snapshot to the synced journal. A crash cannot roll back
previously exposed committed measurements (within displayed rounding). Valve
closure does not wait for disk I/O; final totals are persisted before their summary
is published. Recovery never adds water or elapsed time from time spent offline.
This is a durable simulated-meter guarantee, not a claim to measure physical water
between disk commits. Existing unversioned checkpoints remain readable; new local
checkpoints carry version 1. Broker acknowledgements confirm Kafka delivery, not
Central processing, and a crash around acknowledgement may still cause a replay.

Central integration: healthy NAP reports indicate Monitor liveness and must not
erase Watering/Out-of-service state. Central must mark a lost healthy Monitor
connection as Disconnected (including EOF), while an acknowledged KO remains a
leak until reactivation. Telemetry must not override Monitor-derived disconnection.

Build argument example:

```bash
docker build -f WM_WS/WM_WS_M/Dockerfile --build-arg WS_ID=2 --build-arg CENTRAL_HOSTNAME=altaria.proxy.rlwy.net --build-arg CENTRAL_PORT=40931 -t wm-monitor_station2 WM_WS
```

Build-only Docker arguments:

| Image   | Argument          | Default | Purpose                                             |
| ------- | ----------------- | ------- | --------------------------------------------------- |
| Both    | `BUILD_TESTING` | `OFF` | Build unit tests.                                   |
| Monitor | `BUILD_GUI`     | `OFF` | Build the Qt6 GUI instead of the terminal frontend. |

Use `BUILD_GUI=OFF` for these Docker terminal examples. GUI builds use the same
CLI options but need a graphical display; see [the GUI guide](WM_WS_M/docs/gui.md).

## Run automated unit tests in Docker

These use separate image tags and do not require the external Central or Kafka
services. Building the tests still requires Internet access for dependencies.

```bash
docker build -f WM_WS/WM_WS_E/Dockerfile --build-arg BUILD_TESTING=ON -t wm-engine_tests WM_WS
docker run --rm wm-engine_tests ctest --test-dir build --output-on-failure
docker build -f WM_WS/WM_WS_M/Dockerfile --build-arg BUILD_TESTING=ON --build-arg BUILD_GUI=OFF -t wm-monitor_tests WM_WS
docker run --rm wm-monitor_tests ctest --test-dir build --output-on-failure
```

The same process integration suite can run entirely inside Linux containers,
using the two test images built above:

```sh
docker build -f WM_WS/tests/Dockerfile -t wm-process_tests WM_WS
docker run --rm wm-process_tests
```

To validate the optional GUI without a display server:

```sh
docker build -f WM_WS/WM_WS_M/Dockerfile --build-arg BUILD_TESTING=ON --build-arg BUILD_GUI=ON -t wm-monitor-gui_tests WM_WS
docker run --rm -e QT_QPA_PLATFORM=offscreen -e LANG=C.UTF-8 wm-monitor-gui_tests ctest --test-dir build --output-on-failure
```

## Process integration tests

See [the acceptance matrix](ACCEPTANCE.md) for requirement coverage, recovery
guarantees, and the remaining full-system evaluation checks.

Process tests exercise the terminal frontend. Build Monitor in a separate CLI
folder so a GUI configuration in the normal build folder does not change their
behavior. Stop any manual native test processes, then run:

```sh
cmake -S WM_WS/WM_WS_M -B WM_WS/WM_WS_M/build/cli -DBUILD_GUI=OFF -DBUILD_TESTING=OFF
cmake --build WM_WS/WM_WS_M/build/cli
cmake -S WM_WS/tests -B WM_WS/WM_WS_M/build/process-tests \
  -DENGINE_EXECUTABLE="$PWD/WM_WS/WM_WS_E/build/WM_WS_E" \
  -DMONITOR_EXECUTABLE="$PWD/WM_WS/WM_WS_M/build/cli/WM_WS_M"
cmake --build WM_WS/WM_WS_M/build/process-tests
ctest --test-dir WM_WS/WM_WS_M/build/process-tests --output-on-failure
```

Use the `.exe` suffix on Windows. These tests use the same installed-first
Boost and GoogleTest fallbacks as the applications.

## Cleanup

Stop and remove the two containers when testing is finished. These commands work
for either Docker workflow and keep images, logs, and recovery volumes:

```sh
docker stop wm-monitor wm-engine
docker rm wm-monitor wm-engine
```

After removing both containers, remove the network for your chosen workflow:

```sh
# Direct docker run workflow
docker network rm wm-network
```

```sh
# Compose run workflow with project ws-1
docker network rm ws-1_default
```

Skip `docker stop` for containers that have already exited. Leave volumes in place
to preserve station recovery data and logs for the next run.
