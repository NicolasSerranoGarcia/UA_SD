# Building and running WM_W

WM_W polls OpenWeather for each tracked city every four seconds, serves the latest readings over HTTP, and notifies Central about weather restrictions. Use `--check-config` to validate settings before starting the service.

## Local and lab machines

Copy `WM_W/env.example` to `WM_W/.env` and fill in `OPENWEATHER_API_KEY` locally, or bring your existing `.env` to the lab. Compose reads this file directly. `CENTRAL_API_URL` can use the example value for the offline configuration check. Git and the Docker build context exclude `.env` and `config/cities.json`.

From the repository root, run:

```sh
test -f WM_W/config/cities.json || cp WM_W/config/cities.example.json WM_W/config/cities.json
docker compose -f WM_W/compose.yaml -p wm-weather run --rm --build weather --check-config
docker compose -f WM_W/compose.yaml -p wm-weather run --rm --service-ports weather
```

From another terminal, query the running service:

```sh
curl http://localhost:8080/health
curl http://localhost:8080/weather/alicante
```

Use one WM_W container on Lab PC 1 for the evaluation.
The host port defaults to `8080`. Set `WM_W_HOST_PORT` in `.env` if that port is in use.

To launch an independent container without Compose, use the same local environment file.

```sh
docker build -t wm-weather WM_W
docker run --rm --env-file WM_W/.env -p 8080:8080 \
  --mount "type=bind,source=$(pwd)/WM_W/config,target=/etc/wm-w,readonly" \
  wm-weather
```

The direct Docker example assumes `WM_W_PORT=8080`. Use the actual container port on the right side of `-p` if you change it.

## Native build

Use CMake 3.21 or newer, a C++23 compiler, Boost 1.83 or newer with Program_options, URL, JSON and Beast headers, and libcurl 7.85 or newer. The test build also needs Python 3. Run these commands from the repository root.

```sh
cmake -S WM_W -B WM_W/build -DBUILD_TESTING=ON
cmake --build WM_W/build
ctest --test-dir WM_W/build --output-on-failure
```

| Command-line option       | Environment variable    | Purpose                                                                               |
| ------------------------- | ----------------------- | ------------------------------------------------------------------------------------- |
|                           | `OPENWEATHER_API_KEY` | Required in the process environment.                                                  |
| `--central-api-url`     | `CENTRAL_API_URL`     | Required HTTP(S) base URL.                                                            |
| `--openweather-api-url` |                         | Defaults to OpenWeather HTTPS. Accepts HTTP on `127.0.0.1` for a local mock.        |
| `--port`                | `WM_W_PORT`           | Weather HTTP port. Defaults to `8080`.                                              |
| `--config-file`         | `WM_W_CONFIG_FILE`    | City JSON file. Defaults to `config/cities.json` relative to the working directory. |

Command-line options take precedence over the environment. Run `./build/WM_W --help` from `WM_W/` to see all the flags. With the required settings supplied, run `./build/WM_W` to start, or add `--check-config` to validate settings offline. On Windows with MinGW, use `./build/WM_W.exe`.

The city file defines lowercase city slugs, their OpenWeather coordinates, and an optional mapping from station IDs to those slugs. In Docker, `ENV WM_W_CONFIG_FILE=/etc/wm-w/cities.json` points at the file in the mounted `WM_W/config` directory. The Docker `ENTRYPOINT` runs WM_W directly and passes it any arguments you put after the image name or Compose service name. For example, to check the shipped example file without copying it first, run:

```sh
docker compose -f WM_W/compose.yaml -p wm-weather run --rm --build weather \
  --check-config --config-file /etc/wm-w/cities.example.json
```

## Weather data and alerts

Edit the city configuration to add cities, change coordinates, or reassign a station. WM_W reads it on each polling cycle. If an edit is incomplete or invalid, WM_W keeps using the last valid configuration. Keep existing station IDs when reassigning a station so its prior restriction stays in effect until WM_W has a reading for the new city.

## REST connections

| Direction | Destination and port (TCP) | Method | Resource |
| --- | --- | --- | --- |
| WM_W → OpenWeather | `https://api.openweathermap.org`, HTTPS `443` | `GET` | `/data/2.5/weather` |
| WM_W → API_Central | `CENTRAL_API_URL`, usually HTTPS `443` in the cloud | `PUT` | `/api/stations/{id}/weather-restriction` (proposed) |
| Central → WM_W | Lab host, HTTP `8080` by default | `GET` | `/weather/{city}` |
| Central → WM_W | Lab host, HTTP `8080` by default | `GET` | `/health` |

`CENTRAL_API_URL` is the base address before `/api/stations/...`. An explicit port in that URL takes precedence. Without one, HTTPS uses `443` and HTTP uses `80`. [Central's local Flask server](../WM_Central/WM_Central.py) defaults to `WEB_PORT=8000`. If both modules run natively on the same PC, WM_W can use `http://localhost:8000`. In a container, use a Central hostname reachable from that container.

For the reverse connection, `WM_W_PORT` sets the port inside the container and `WM_W_HOST_PORT` sets the port published on the lab PC by Compose. Both default to `8080`. Central needs a route to that published port. If an HTTPS reverse proxy is used, Central calls the proxy's HTTPS address, and the proxy forwards to WM_W's HTTP listener.

### WM_W polls OpenWeather

Every four seconds, WM_W sends a GET request for each distinct tracked city. The query uses `lat` and `lon` from the city file, `units=metric` for Celsius, and `appid` from `OPENWEATHER_API_KEY`. WM_W uses `main.temp`, `weather[].id` and `dt` from a successful response. [OpenWeather Current Weather API](https://openweathermap.org/api/current)

### WM_W sends a restriction to Central

`PUT /api/stations/{id}/weather-restriction` uses the numeric station ID from the city configuration. The request has `Content-Type: application/json` and a body such as:

```json
{"city":"alicante","active":true}
```

`active:true` means rain or a temperature below 0 °C. `active:false` clears the weather restriction. WM_W sends the initial state after the first valid sample, then sends changes and retries failed writes. Central should apply updates in order per station, handle repeated requests safely and return `2xx`. WM_W requires no response body.

This route is a proposed contract. The current Central API still needs to implement it. Central and Engine apply the restriction, including allowing an irrigation already in progress to finish normally.

### Central reads weather and checks liveness

`GET /weather/alicante` uses the lowercase city slug from the city configuration. Neither GET endpoint needs a request body. WM_W replies with JSON and `Cache-Control: no-store`.

```json
{
  "city": "alicante",
  "temperature_c": 12.5,
  "raining": false,
  "restricted": false,
  "observed_at": "2026-11-23T09:00:00Z",
  "fetched_at": "2026-11-23T09:01:04Z",
  "stale": false
}
```

`GET /weather/{city}` returns `200` with the latest saved sample, `404` for an untracked city and `503` while waiting for the first valid reading. The timestamps distinguish when OpenWeather observed the conditions from when WM_W fetched them. The last reading is marked `stale:true` after a failed poll or when the last successful fetch was more than eight seconds ago. The existing restriction stays in effect.

`GET /health` returns `200` with `{"status":"ok"}` when WM_W can answer HTTP requests. Use the weather resource to check reading availability and freshness. The four-second poll happens between WM_W and OpenWeather. Central receives restriction updates and can read the current weather when needed.
