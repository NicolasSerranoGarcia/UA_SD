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

## Optional Qt support

The Monitor's `BUILD_GUI` option defaults to `OFF`. With it disabled, Qt is
not searched for, downloaded, built, or linked. Enable the dependency support
with:

```sh
cmake -S WM_WS/WM_WS_M -B WM_WS/WM_WS_M/build/gui -DBUILD_GUI=ON
cmake --build WM_WS/WM_WS_M/build/gui
```

`cmake/qt.cmake` defines the `wm_ws_qt` interface library, linking Qt6 Core,
Gui, and Widgets. Other Monitor libraries that use Qt should link this target.
AUTOMOC and AUTORCC are enabled on the Monitor executable only; AUTOUIC is
disabled. This prepares the build for a GUI; the application still runs its
existing console entry point.

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

## Docker

Build from the `WM_WS` context so shared headers and CMake modules are copied:

```sh
docker build -f WM_WS/WM_WS_M/Dockerfile -t wm-monitor WM_WS
docker build -f WM_WS/WM_WS_M/Dockerfile --build-arg BUILD_GUI=ON -t wm-monitor-gui WM_WS
docker build -f WM_WS/WM_WS_E/Dockerfile -t wm-engine WM_WS
```

The Monitor image includes Qt6 development packages. Qt is linked only when
`BUILD_GUI=ON`. A future graphical application will also need access to a
display server when run inside a container.

## Run on the same Docker network

These containers have separate network namespaces and communicate over a Docker
bridge network. No host port publishing is needed. The monitor currently requires
a numeric engine IPv4 address; a Docker container name is not accepted.

Run this block once to create the network and start both modules. The engine uses
`-i` so its console remains available for fault injection through `docker attach`.

```bash
docker network create wm-network
docker volume create wm-engine-state
docker run -di --name wm-engine --network wm-network -v wm-engine-state:/var/lib/wm-ws wm-engine
ENGINE_IP=$(docker inspect -f '{{(index .NetworkSettings.Networks "wm-network").IPAddress}}' wm-engine)
docker run -d --name wm-monitor --network wm-network -e WS_ID=1 -e ENGINE_IP="$ENGINE_IP" wm-monitor
docker logs wm-engine
docker logs -f wm-monitor
```

Press `Ctrl+C` to stop following logs; the containers continue running. If the
network already exists, skip `docker network create`. Existing containers with
these names must be removed using the cleanup commands below before rerunning.
Reinspect the engine IP if its container is recreated.

For PowerShell, use the same commands but replace the `ENGINE_IP=...` assignment
with:

```powershell
$ENGINE_IP = docker inspect -f '{{(index .NetworkSettings.Networks "wm-network").IPAddress}}' wm-engine
```

Expected results:

- Engine logs show `listening on 0.0.0.0:8080` (the current log prefix is `[Monitor][HCP]`).
- Monitor logs show registration acknowledged, or already registered, followed by activation acknowledged.
- Monitor logs show `[Engine][HCP] ws=1 health=OK` and Central status acknowledgements with `state=healthy`.

The default `ENGINE_IP=127.0.0.1` only works when both processes share a network
namespace, or when running both executables directly on the same host. Override
it as above for separate containers on a bridge network.

## Test faults and recovery

In one terminal, follow the monitor:

```bash
docker logs -f wm-monitor
```

In another terminal, attach to the engine:

```bash
docker attach wm-engine
```

Type `f` and press Enter to inject a fault. The monitor should receive `health=KO`
and Central should acknowledge `state=fault`. Type `r` and press Enter to repair;
the monitor should return to `health=OK` and Central should acknowledge
`state=healthy`.

Type `s` to start a manual irrigation when activated, and `e` to end it. Manual
starts publish `ws-start-conf` with `station_id` and `operation_code: null`;
Central assigns the operation code internally. A Central `BLOCK` ends the current
irrigation and prevents further manual starts. Only an external `ACTIVATE` can
clear that block; it does not resume the old irrigation.

Repair returns Engine to idle. Monitor sends a new NAP activation and waits for
Central's acknowledgement before sending HCP `AC`. With Monitor stopped, Engine
stops watering, rejects starts, and withholds Kafka output until reactivated.

Detach with `Ctrl+P`, then `Ctrl+Q` to leave the engine running. The engine also
accepts `q` to exit; avoid it unless testing shutdown.

To test loss of the engine connection and subsequent recovery:

```bash
docker stop wm-engine
docker start wm-engine
```

Allow the monitor to detect the failure before restarting the engine. While the
engine is stopped, the monitor reports KO to Central and retries the engine
connection. After restart it should reconnect and report OK.

`[Central][NAP] ... status acknowledged: state=fault reason=connect: ... Connection refused`
means Central accepted a report of an engine connection failure. The `reason`
text is a local diagnostic; the NAP status frame carries `KO`, not that text.
Actual Central reporting failures are logged as `status send failed` or
`status acknowledgement failed`.

## Executable parameters

These are command-line parameters for the executables themselves. CLI defaults
are separate from the Docker defaults listed later. Ports must be in `1..65535`;
all timeout, interval, and retry values must be positive milliseconds.

### Engine: WM_WS_E

| Parameter | CLI default | Purpose |
| --- | --- | --- |
| `--help` | â€” | Print available options and exit. |
| `--queue HOST:PORT` | Required | Kafka bootstrap endpoint; must be nonempty. |
| `--hcp-port PORT` | `8080` | Local TCP port for monitor health connections, listening on all IPv4 interfaces. |
| `--hcp-timeout-ms MS` | `2000` | HCP receive/send deadline. |
| `--state-file PATH` | `wm-ws-state.jsonl` | Base path for two synced checkpoint files (`.0` and `.1`); use a separate path per station. |

Copy-and-paste example with every operational parameter:

```bash
docker run --rm -it --network wm-network wm-engine ./build/WM_WS_E --queue iriguchi.proxy.rlwy.net:37040 --hcp-port 8080 --hcp-timeout-ms 2000
```

This launches an additional engine; stop it before continuing tests that assume
only one engine. The monitor must target this container's IP to test it.

### Monitor: WM_WS_M

| Parameter | CLI default | Purpose |
| --- | --- | --- |
| `--help` | â€” | Print available options and exit. |
| `--ws-id ID` | Required | Station ID: 1â€“64 letters, digits, hyphens, or underscores. |
| `--engine-ip IPV4` | Required | Engine address in dotted-decimal IPv4 format. Hostnames are not supported. |
| `--engine-port PORT` | `8080` | Engine HCP listening port. |
| `--hcp-timeout-ms MS` | `2000` | HCP connect/send/response deadline. |
| `--hcp-retry-ms MS` | `1000` | Delay between engine connection attempts. |
| `--nap-interval-ms MS` | `1000` | Interval for healthy reports and retries of unacknowledged fault reports. |
| `--nap-timeout-ms MS` | `2000` | NAP connect/send/response deadline. |
| `--central-hostname HOST` | Required | Nonempty Central hostname; resolved to IPv4. |
| `--central-port PORT` | Required | Central NAP TCP port. |
| `--monitor-location LOCATION` | `spain` | Registration location; nonempty and must not contain `#`. |

To run with every operational parameter, stop the existing monitor first. This
example reuses the engine started in the bridge network section:

```bash
docker stop wm-monitor
ENGINE_IP=$(docker inspect -f '{{(index .NetworkSettings.Networks "wm-network").IPAddress}}' wm-engine)
docker run --rm -it --network wm-network wm-monitor ./build/WM_WS_M --ws-id 1 --engine-ip "$ENGINE_IP" --engine-port 8080 --hcp-timeout-ms 2000 --hcp-retry-ms 1000 --nap-interval-ms 1000 --nap-timeout-ms 2000 --central-hostname altaria.proxy.rlwy.net --central-port 40931 --monitor-location spain
```

Print executable help without connecting to the services:

```bash
docker run --rm wm-engine ./build/WM_WS_E --help
docker run --rm wm-monitor ./build/WM_WS_M --help
```

An explicit executable command after the image name replaces the entire Docker
default command. Supply all required CLI arguments in that case; environment
variables are translated into CLI arguments only by the default Docker command.

## Docker parameters

Each connection parameter below is both a build argument (`--build-arg`) and an
environment variable (`docker run -e`). A build argument changes the image's
default; a runtime environment variable overrides that default for one container.

| Image | Parameter | Default | Executable argument |
| --- | --- | --- | --- |
| `wm-engine` | `QUEUE_ENDPOINT` | `iriguchi.proxy.rlwy.net:37040` | `--queue` |
| `wm-engine` | `STATE_FILE` | `/var/lib/wm-ws/state.jsonl` | `--state-file` |
| `wm-monitor` | `WS_ID` | `1` | `--ws-id` |
| `wm-monitor` | `ENGINE_IP` | `127.0.0.1` | `--engine-ip` |
| `wm-monitor` | `ENGINE_PORT` | `8080` | `--engine-port` |
| `wm-monitor` | `CENTRAL_HOSTNAME` | `altaria.proxy.rlwy.net` | `--central-hostname` |
| `wm-monitor` | `CENTRAL_PORT` | `40931` | `--central-port` |

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
The recovery summary uses the last persisted measurements, checkpointed every second
while watering; it never invents water measurements from time spent offline.

Central integration: healthy NAP reports indicate Monitor liveness and must not
erase Watering/Out-of-service state. Central must mark a lost healthy Monitor
connection as Disconnected (including EOF), while an acknowledged KO remains a
leak until reactivation. Telemetry must not override Monitor-derived disconnection.

Build argument example:

```bash
docker build -f WM_WS/WM_WS_M/Dockerfile --build-arg WS_ID=2 --build-arg CENTRAL_HOSTNAME=altaria.proxy.rlwy.net --build-arg CENTRAL_PORT=40931 -t wm-monitor_station2 WM_WS
```

Build-only Docker arguments:

| Image | Argument | Default | Purpose |
| --- | --- | --- | --- |
| Both | `BUILD_TESTING` | `OFF` | Build unit tests. |
| Monitor | `BUILD_GUI` | `OFF` | Build the Qt6 GUI instead of the terminal frontend. |

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


## Process integration tests

After building both executables, stop any manual native test processes and run:

```sh
cmake -S WM_WS/tests -B WM_WS/WM_WS_M/build/process-tests \
  -DENGINE_EXECUTABLE="$PWD/WM_WS/WM_WS_E/build/WM_WS_E" \
  -DMONITOR_EXECUTABLE="$PWD/WM_WS/WM_WS_M/build/WM_WS_M"
cmake --build WM_WS/WM_WS_M/build/process-tests
ctest --test-dir WM_WS/WM_WS_M/build/process-tests --output-on-failure
```

Use the `.exe` suffix on Windows. These tests use the same installed-first
Boost and GoogleTest fallbacks as the applications.

## Cleanup

Stop following or attaching to the containers before removing them. These commands
remove only the two named test containers and their network; images and the station
state volume are retained.

```bash
docker stop wm-monitor wm-engine
docker rm wm-monitor wm-engine
docker network rm wm-network
```
