"""Run the Monitor menus against a temporary HTTPS Registry."""

import argparse
import os
import ssl
import subprocess
import tempfile
import threading
from pathlib import Path

from flask import request
from werkzeug.serving import make_server

from wm_registry.app import create_app
from wm_registry.store import reset_enrollment


def run(cli, gui):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cert, key = root / "registry.crt", root / "registry.key"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
                        "-keyout", str(key), "-out", str(cert)], check=True, capture_output=True)
        db = root / "test.db"
        app = create_app(db)
        issued = []
        drop = set()

        @app.after_request
        def lose_reply(response):
            if response.status_code == 201:
                issued.append(response.json["credential"])
            if request.method in drop and response.status_code in (201, 204):
                return app.response_class(status=503)
            return response

        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(str(cert), str(key))
        server = make_server("127.0.0.1", 0, app, threaded=True, ssl_context=context)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()

        def params(station, trust=True):
            args = ["--registry-url", f"https://127.0.0.1:{server.server_port}", "--ws-id", str(station),
                    "--log-file", str(root / "monitor.log"), "--registry-state-dir", str(root / "state")]
            return args + (["--registry-ca-file", str(cert)] if trust else [])

        def terminal(commands, expected, trust=True):
            result = subprocess.run([str(cli), *params(1, trust)], input=commands.encode(),
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
            output = result.stdout.decode(errors="replace")
            assert result.returncode == 0 and expected in output, output
            assert all(secret not in output for secret in issued)
            return output

        saved = root / "state" / "1.credential"
        try:
            terminal("r\nq\n", "Registry HTTPS request failed", trust=False)
            assert not saved.exists()
            terminal("r\nq\n", "Registered. Awaiting secure Central activation.")
            assert saved.is_file()
            if os.name == "nt":
                assert issued[-1].encode() not in saved.read_bytes()
            else:
                assert not (saved.stat().st_mode & 0o077)
            terminal("q\n", "Registered. Awaiting secure Central activation.")
            drop.add("DELETE")
            terminal("d\nq\n", "The saved credential was kept")
            assert saved.is_file()
            terminal("q\n", "Registry no longer accepts the saved credential")
            assert not saved.exists()
            drop.clear()

            drop.add("PUT")
            terminal("r\nq\n", "Registration failed (HTTP 503)")
            assert not saved.exists()
            drop.clear()
            terminal("r\nq\n", "This station ID is already registered.")
            assert reset_enrollment(db, 1)
            terminal("r\nd\nq\n", "Deregistered. The local credential was removed.")
            assert not saved.exists()

            if gui is not None:
                result = subprocess.run([str(gui), *params(2)], timeout=25, capture_output=True,
                                        env={**os.environ, "QT_QPA_PLATFORM": "offscreen"})
                assert result.returncode == 0, result.stdout + result.stderr
                assert not (root / "state" / "2.credential").exists()
                assert all(secret.encode() not in result.stdout + result.stderr for secret in issued)
            assert all(secret not in (root / "monitor.log").read_text() for secret in issued)
        finally:
            server.shutdown()
            server.server_close()
            worker.join()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("cli", type=lambda path: Path(path).resolve())
    parser.add_argument("--gui-driver", type=lambda path: Path(path).resolve())
    arguments = parser.parse_args()
    run(arguments.cli, arguments.gui_driver)
    print("Monitor HTTPS enrollment checks passed.")
