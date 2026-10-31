import os
import uuid
import threading
from flask import Flask, jsonify, request, send_from_directory

import modules.utils.DBManager as db
import modules.shared.shared_state as st
from modules.kafka_clients import send_event

app = Flask(__name__, static_folder="static")

# Estados desde los que se permite cada comando (mismas reglas que fo_req_handler)
ALLOWED = {
    "IRRIGATION": ["Available"],
    "BLOCK": ["Available", "Watering"],
    "ACTIVATE": ["Out of service"],
    "STOP": ["Watering"],
}

def _valid_duration(duration) -> bool:
    """None = indefinite irrigation. Otherwise it must be a positive integer."""
    if duration is None:
        return True
    return isinstance(duration, int) and not isinstance(duration, bool) and duration > 0

def _dispatch(station_id, cmd, duration=None):
    status = db.get_station_status(station_id)
    if status not in ALLOWED[cmd]:
        return False, f"Cannot {cmd} a station in status '{status}'"
    code = uuid.uuid4().int
    msg = {"operator_id": None, "station_id": station_id, "type": cmd, "operation_code": code}
    if cmd == "IRRIGATION":
        msg["duration"] = duration           # None -> null in the JSON (indefinite)
    st.operation_central_dict[code] = cmd      # se registra ANTES de enviar
    kcli_send(msg, station_id)
    if cmd == "IRRIGATION":
        st.log(f"Central sent {cmd} command to WS-{station_id} "
               f"({'indefinite duration' if duration is None else f'duration {duration}'})")
    else:
        st.log(f"Central sent {cmd} command to WS-{station_id}")
    return True, code

def kcli_send(msg, station_id):
    send_event(topic="central-ops", msg=msg, key=station_id)

@app.get("/")
def index():
    return send_from_directory(app.static_folder, "index.html")

@app.get("/api/stations")
def stations():
    out = []
    for i, loc, status, op in db.read_all_stations_detailed():
        t = st.telemetry.get(i)
        watering = status == "Watering" and t is not None
        out.append({
            "id": i, "location": loc, "status": status,
            "operator": op if status == "Watering" else None,
            "flux": round(t["flux"], 1) if watering else None,
            "volume": round(t["volume"], 1) if watering else None,
        })
    return jsonify(out)

@app.get("/api/events")
def events():
    return jsonify(st.get_events())

@app.post("/api/stations/<int:station_id>/command")
def command(station_id):
    body = request.get_json(silent=True) or {}
    cmd, duration = body.get("command"), body.get("duration")
    if cmd not in ALLOWED:
        return jsonify(error="Invalid command"), 400
    if not db.check_station(station_id):
        return jsonify(error=f"Station {station_id} does not exist"), 404
    if cmd == "IRRIGATION" and not _valid_duration(duration):
        return jsonify(error="duration must be a positive integer or empty"), 400
    ok, res = _dispatch(station_id, cmd, duration)
    return (jsonify(ok=True, operation_code=res), 202) if ok else (jsonify(error=res), 409)

@app.post("/api/command-all")
def command_all():
    body = request.get_json(silent=True) or {}
    cmd, duration = body.get("command"), body.get("duration")
    if cmd not in ALLOWED:
        return jsonify(error="Invalid command"), 400
    if cmd == "IRRIGATION" and not _valid_duration(duration):
        return jsonify(error="duration must be a positive integer or empty"), 400
    sent, skipped = [], []
    for station_id, *_ in db.read_all_stations_detailed():
        ok, _res = _dispatch(station_id, cmd, duration)
        (sent if ok else skipped).append(station_id)
    return jsonify(sent=sent, skipped=skipped), 202

def start_web(port: int):
    threading.Thread(
        target=lambda: app.run(host="0.0.0.0", port=port, threaded=True, use_reloader=False),
        daemon=True,
    ).start()
    print(f"Web UI listening on port {port}")