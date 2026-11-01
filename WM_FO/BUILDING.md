# WM_FO - Field Operator

## Requirements

- Docker
- `WM_Central` and Kafka running (Railway)

## Project structure

```
WM_FO/
├── Dockerfile
├── requirements.txt
├── WM_FO.py
└── modules/
    ├── kafka_clients.py
    ├── utils.py
    ├── shared/
    │   └── shared_status.py
    └── web/
        ├── web_api.py
        └── static/
            └── index.html
```

`requirements.txt`:

```
flask
confluent-kafka
```

## Build

```bash
docker build -t wm_fo .
```

Rebuild every time a file changes.

## Run

```bash
docker run --rm -p 8001:8001 wm_fo 1 --web-port 8001
```

- `1`: operator ID
- `--web-port`: web UI port (default `8000 + ID`). It must match the port in `-p`.
- `--kafka-ip` / `--kafka-port`: optional, to use a Kafka other than the default.

For a second operator, in another terminal:

```bash
docker run --rm -p 8002:8002 wm_fo 2 --web-port 8002
```

The web UI is available at <http://localhost:8001>.