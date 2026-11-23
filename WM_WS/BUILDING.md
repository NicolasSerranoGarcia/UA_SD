# Watering station commands

Run from the repository root in Bash. Use a C++23 compiler and CMake 3.21+.
On Windows, add `.exe` to executable paths; some generators also add `Release/`.
Use a separate terminal for each Engine and Monitor command.

For the Release 2 enrollment menu, see [WM_Registry deployment and Monitor commands](../WM_Registry/BUILDING.md). Monitor uses the Railway Registry by default. The existing Engine and Monitor commands below select NAP monitoring through their Central settings.

## 1. Build and run from source

### Build Engine

```sh
cmake -S WM_WS/WM_WS_E -B WM_WS/WM_WS_E/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DBUILD_EXAMPLES=OFF
cmake --build WM_WS/WM_WS_E/build --config Release --parallel 2
```

### Build Monitor — Qt (`ON`) or terminal (`OFF`)

```sh
cmake -S WM_WS/WM_WS_M -B WM_WS/WM_WS_M/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DBUILD_GUI=ON
cmake --build WM_WS/WM_WS_M/build --config Release --parallel 2
```

### Run Engine — terminal 1

```sh
./WM_WS/WM_WS_E/build/WM_WS_E \
  --queue iriguchi.proxy.rlwy.net:37040 \
  --hcp-port 8080 \
  --hcp-timeout-ms 2000 \
  --state-file state/wm-ws-state.jsonl \
  --log-file log/wm-ws-engine.log
```

### Run Monitor — terminal 2

```sh
# Alternative: replace --engine-hostname localhost with --engine-ip 127.0.0.1.
./WM_WS/WM_WS_M/build/WM_WS_M \
  --ws-id 1 \
  --engine-hostname localhost \
  --engine-port 8080 \
  --hcp-timeout-ms 2000 \
  --hcp-retry-ms 1000 \
  --nap-interval-ms 1000 \
  --nap-timeout-ms 2000 \
  --central-hostname altaria.proxy.rlwy.net \
  --central-port 40931 \
  --monitor-location alicante \
  --log-file log/wm-ws-monitor.log
```

## 2. Build and run Docker images

### Build images — terminal Monitor

```sh
docker build -f WM_WS/WM_WS_E/Dockerfile \
  --build-arg BUILD_TESTING=OFF \
  -t wm-engine WM_WS

docker build -f WM_WS/WM_WS_M/Dockerfile \
  --build-arg BUILD_TESTING=OFF \
  --build-arg BUILD_GUI=OFF \
  -t wm-monitor WM_WS
```

### Create the network

```sh
docker network create wm-network
```

### Run Engine — terminal 1

```sh
docker run -it --name wm-engine-1 --network wm-network \
  -v wm-engine-state-1:/var/lib/wm-ws \
  -v wm-station-logs-1:/var/log/wm-ws \
  wm-engine ./build/WM_WS_E \
  --queue iriguchi.proxy.rlwy.net:37040 \
  --hcp-port 8080 \
  --hcp-timeout-ms 2000 \
  --state-file /var/lib/wm-ws/state.jsonl \
  --log-file /var/log/wm-ws/wm-ws-engine.log
```

### Run Monitor — terminal 2

```sh
docker run -it --name wm-monitor-1 --network wm-network \
  -v wm-station-logs-1:/var/log/wm-ws \
  wm-monitor ./build/WM_WS_M \
  --ws-id 1 \
  --engine-hostname wm-engine-1 \
  --engine-port 8080 \
  --hcp-timeout-ms 2000 \
  --hcp-retry-ms 1000 \
  --nap-interval-ms 1000 \
  --nap-timeout-ms 2000 \
  --central-hostname altaria.proxy.rlwy.net \
  --central-port 40931 \
  --monitor-location alicante \
  --log-file /var/log/wm-ws/wm-ws-monitor.log
```

## 3. Build and run with Compose

After quitting a module, rerun the same command in its terminal. The `--rm` flag removes the container when it exits. Keep the same project name (`-p ws-1`) for both commands so they share a network and reuse the station's log and state volumes.

### Engine — terminal 1

```sh
docker compose -f WM_WS/compose.yaml -p ws-1 run \
  --rm --build --no-deps --use-aliases \
  engine ./build/WM_WS_E \
  --queue iriguchi.proxy.rlwy.net:37040 \
  --hcp-port 8080 \
  --hcp-timeout-ms 2000 \
  --state-file /var/lib/wm-ws/state.jsonl \
  --log-file /var/log/wm-ws/wm-ws-engine.log
```

### Monitor — terminal 2

```sh
docker compose -f WM_WS/compose.yaml -p ws-1 run \
  --rm --build --no-deps \
  monitor ./build/WM_WS_M \
  --ws-id 1 \
  --engine-hostname engine \
  --engine-port 8080 \
  --hcp-timeout-ms 2000 \
  --hcp-retry-ms 1000 \
  --nap-interval-ms 1000 \
  --nap-timeout-ms 2000 \
  --central-hostname altaria.proxy.rlwy.net \
  --central-port 40931 \
  --monitor-location alicante \
  --log-file /var/log/wm-ws/wm-ws-monitor.log
```

## Restart and cleanup

### Restart Engine created with `docker run` — terminal 1

```sh
docker start -ai wm-engine-1
```

### Restart Monitor created with `docker run` — terminal 2

```sh
docker start -ai wm-monitor-1
```

### Stop and delete `docker run` containers for station 1

```sh
docker stop wm-monitor-1 wm-engine-1
docker rm wm-monitor-1 wm-engine-1
```

### Stop and delete `docker run` containers for station 2

```sh
docker stop wm-monitor-2 wm-engine-2
docker rm wm-monitor-2 wm-engine-2
```

### Remove Compose projects — keep volumes

```sh
docker compose -f WM_WS/compose.yaml -p ws-1 down --remove-orphans
docker compose -f WM_WS/compose.yaml -p ws-2 down --remove-orphans
```

### Remove the direct Docker network

```sh
docker network rm wm-network
```

### Delete direct Docker station 1 data and logs — after deleting its containers

```sh
docker volume rm wm-engine-state-1 wm-station-logs-1
```

### Delete Compose station data, logs, and Registry credentials

```sh
docker compose -f WM_WS/compose.yaml -p ws-1 down --volumes --remove-orphans
docker compose -f WM_WS/compose.yaml -p ws-2 down --volumes --remove-orphans
```

If the station is still registered after removing the credential volume, reset its enrollment in Registry before registering again.

### Reset native build directories

```sh
rm -rf WM_WS/WM_WS_E/build WM_WS/WM_WS_M/build
```

### Rebuild Compose images without cache

```sh
docker compose -f WM_WS/compose.yaml -p ws-1 build --no-cache --pull engine monitor
```
