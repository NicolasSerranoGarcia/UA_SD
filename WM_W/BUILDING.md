# Building and running WM_W

WM_W currently validates its environment and city configuration, then exits. This check requires no network access.

## Local and lab machines

Copy `WM_W/env.example` to `WM_W/.env` and fill in `OPENWEATHER_API_KEY` locally, or bring your existing `.env` to the lab. Compose reads this file directly. `CENTRAL_API_URL` can use the example value for the offline configuration check. Git and the Docker build context exclude `.env` and `config/cities.json`.

From the repository root, run:

```sh
test -f WM_W/config/cities.json || cp WM_W/config/cities.example.json WM_W/config/cities.json
docker compose -f WM_W/compose.yaml -p wm-weather run --rm --build weather --check-config
```

Use one WM_W container on Lab PC 1 for the evaluation.

To launch an independent container without Compose, use the same local environment file.

```sh
docker build -t wm-weather WM_W
docker run --rm --env-file WM_W/.env \
  --mount "type=bind,source=$(pwd)/WM_W/config,target=/etc/wm-w,readonly" \
  wm-weather --check-config
```

## Native build

Install CMake 3.21 or newer, a C++23 compiler, and Boost.Program_options, Boost.URL and Boost.JSON 1.83 or newer. Run these commands from the repository root.

```sh
cmake -S WM_W -B WM_W/build -DBUILD_TESTING=ON
cmake --build WM_W/build
ctest --test-dir WM_W/build --output-on-failure
```

| Command-line option | Environment variable | Purpose |
| --- | --- | --- |
| | `OPENWEATHER_API_KEY` | Required in the process environment. |
| `--central-api-url` | `CENTRAL_API_URL` | Required HTTP(S) base URL. |
| `--port` | `WM_W_PORT` | HTTP port for the planned server. Defaults to `8080`. |
| `--config-file` | `WM_W_CONFIG_FILE` | City JSON file. Defaults to `config/cities.json` relative to the working directory. |

Command-line options take precedence over the environment. Run `./build/WM_W --help` from `WM_W/` to see all the flags. With the required settings supplied, run `./build/WM_W --check-config`. On Windows with MinGW, use `./build/WM_W.exe`.

The city file defines lowercase city slugs, their OpenWeather coordinates, and an optional mapping from station IDs to those slugs. In Docker, `ENV WM_W_CONFIG_FILE=/etc/wm-w/cities.json` points at the file in the mounted `WM_W/config` directory. The Docker `ENTRYPOINT` runs WM_W directly and passes it any arguments you put after the image name or Compose service name. For example, to check the shipped example file without copying it first, run:

```sh
docker compose -f WM_W/compose.yaml -p wm-weather run --rm --build weather \
  --check-config --config-file /etc/wm-w/cities.example.json
```
