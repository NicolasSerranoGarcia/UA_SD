import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlsplit
from urllib.request import urlopen


class Fixture:
    def __init__(self):
        self.lock = threading.Lock()
        self.condition = (-0.1, [800])
        self.polls = []
        self.writes = []
        self.fail_first_write = True


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, status, body=None):
        data = json.dumps(body).encode() if body is not None else b""
        self.send_response(status)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        target = urlsplit(self.path)
        with self.server.state.lock:
            self.server.state.polls.append((target.path, parse_qs(target.query), time.monotonic()))
            condition = self.server.state.condition
        if condition is None:
            return self.reply(429)
        temp, codes = condition
        self.reply(200, {"main": {"temp": temp}, "weather": [{"id": i} for i in codes],
                         "rain": {"1h": 1}, "dt": 1760000000})

    def do_PUT(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        with self.server.state.lock:
            failed = self.server.state.fail_first_write
            self.server.state.fail_first_write = False
            self.server.state.writes.append((self.path, body, failed))
        self.reply(503 if failed else 204)


def eventually(check, timeout=8):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        result = check()
        if result:
            return result
        time.sleep(0.05)
    raise AssertionError("Expected service behavior was not observed")


def run(executable):
    state = Fixture()
    mock = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    mock.state = state
    worker = threading.Thread(target=mock.serve_forever, daemon=True)
    worker.start()
    with socket.socket() as port_holder:
        port_holder.bind(("127.0.0.1", 0))
        port = port_holder.getsockname()[1]
    upstream = f"http://127.0.0.1:{mock.server_port}"
    base = f"http://127.0.0.1:{port}"

    def get(path):
        try:
            with urlopen(base + path, timeout=0.4) as reply:
                return reply.status, json.load(reply)
        except HTTPError as error:
            return error.code, json.load(error)
        except (OSError, URLError):
            return 0, {}

    def snapshot(city="alicante"):
        return get("/weather/" + city)[1]

    def active_writes(active, city="alicante"):
        with state.lock:
            return [p for p, payload, failed in state.writes if not failed and
                    payload == {"city": city, "active": active}]

    with tempfile.TemporaryDirectory() as folder:
        config = Path(folder) / "cities.json"
        config.write_text(json.dumps({"cities": {"alicante": {"latitude": 38.3, "longitude": -0.4}},
                                      "stations": {"1": "alicante", "2": "alicante"}}), encoding="utf-8")
        output = Path(folder) / "wm-w.log"
        launch = [executable, "--config-file", str(config), "--port", str(port),
                  "--central-api-url", upstream, "--openweather-api-url", upstream]
        environment = {**os.environ, "OPENWEATHER_API_KEY": "dummy-test-key"}
        with socket.socket() as occupied:
            if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
                occupied.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            occupied.bind(("0.0.0.0", port))
            occupied.listen()
            failed = subprocess.run(launch, env=environment, capture_output=True, timeout=5)
            assert failed.returncode != 0, "WM_W should reject a port that is already occupied"
        with state.lock:
            assert not state.polls and not state.writes
        with output.open("w") as log:
            process = subprocess.Popen(launch, env=environment,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                eventually(lambda: process.poll() is not None or get("/health")[0] == 200)
                assert process.poll() is None, "WM_W exited instead of starting the service"
                assert get("/weather/missing")[0] == 404
                eventually(lambda: snapshot().get("restricted") is True)
                eventually(lambda: len(active_writes(True)) == 2)
                assert set(active_writes(True)) == {f"/api/stations/{i}/weather-restriction" for i in (1, 2)}
                first = snapshot()
                assert first["temperature_c"] == -0.1 and not first["raining"] and not first["stale"]
                assert first["observed_at"].endswith("Z") and first["fetched_at"].endswith("Z")
                with state.lock:
                    assert len(state.polls) == 1 and state.writes[0][2]
                    path, query, tick = state.polls[0]
                    assert path == "/data/2.5/weather"
                    assert query["units"] == ["metric"] and query["appid"] == ["dummy-test-key"]
                    assert query["lat"] == ["38.3"] and query["lon"] == ["-0.4"]
                    state.condition = None
                config.write_text("{", encoding="utf-8")
                eventually(lambda: snapshot().get("stale") is True)
                assert snapshot()["restricted"] and not active_writes(False)
                with state.lock:
                    assert 3.5 <= state.polls[1][2] - tick <= 4.8, "Expected a four-second poll"
                    state.condition = (11, [800, 500])
                eventually(lambda: snapshot().get("raining") is True)
                assert len(active_writes(True)) == 2 and not active_writes(False)
                with state.lock:
                    state.condition = None
                config.write_text(json.dumps({"cities": {"elche": {"latitude": 38.2, "longitude": -0.7}},
                                              "stations": {"1": "elche", "2": "elche"}}), encoding="utf-8")
                eventually(lambda: get("/weather/elche")[0] == 503)
                assert get("/weather/alicante")[0] == 404
                assert not active_writes(False) and not active_writes(False, "elche")
                with state.lock:
                    state.condition = (0, [800])
                eventually(lambda: snapshot("elche").get("temperature_c") == 0)
                eventually(lambda: len(active_writes(False, "elche")) == 2)
                assert snapshot("elche")["restricted"] is False
                assert not active_writes(False)
            except Exception:
                print(output.read_text(encoding="utf-8", errors="replace"))
                raise
            finally:
                process.terminate()
                process.wait(timeout=5)
                mock.shutdown()
                mock.server_close()
        assert "dummy-test-key" not in output.read_text(encoding="utf-8")


if __name__ == "__main__":
    run(sys.argv[1])
