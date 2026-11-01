import time
import queue
import uuid
import threading
from flask import Flask, jsonify, request, render_template
from modules.utils import read_file, parse_commands_content
from modules.kafka_clients import send_command
from modules.shared.shared_status import fo_state, state_lock, central_notis_queue, add_log

cmd_lock = threading.Lock() 

app = Flask(__name__, template_folder="static")

def send_and_wait_central(cmd_type: str, station_id: int | None = None, duration: int | None = None, timeout: float = 5.0):
    operation_code = uuid.uuid4().int
    producer = app.config.get('PRODUCER')
    operator_id = app.config.get('OPERATOR_ID')

    with cmd_lock:
        while not central_notis_queue.empty():
            try:
                central_notis_queue.get_nowait()
            except queue.Empty:
                break

        ok = send_command(producer, operator_id, station_id, cmd_type, operation_code, duration)
        if not ok: return {"status": "KO", "reason": "Producer Error"}

        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                response = central_notis_queue.get(timeout=0.3)
                if response.get("operation_code") == operation_code and response.get("type") == "RESPONSE":
                    return response
            except queue.Empty:
                continue
        return {"status": "TIMEOUT", "reason": "Timeout esperando a Central"}

@app.route("/")
def index():
    return render_template("index.html", operator_id=app.config.get('OPERATOR_ID'))

@app.route("/api/state", methods=["GET"])
def api_state():
    with state_lock:
        return jsonify(fo_state)

@app.route("/api/command", methods=["POST"])
def api_command():
    body = request.json or {}
    cmd_type = body.get("type")
    station_id = int(body.get("station_id")) if body.get("station_id") else None
    
    duration_raw = body.get("duration")
    duration = int(duration_raw) if duration_raw else None

    resp = send_and_wait_central(
        cmd_type=cmd_type,
        station_id=station_id,
        duration=duration if cmd_type == "IRRIGATION" else None
    )

    status = resp.get("status")
    if status == "OK":
        with state_lock:
            if cmd_type == "IRRIGATION":
                fo_state["station_served"] = station_id
                fo_state["irrigating"] = True
                dur_text = f"{duration}s" if duration else "indefinido"
                add_log("SUCCESS", f"Riego iniciado en WS-{station_id} ({dur_text})")
            elif cmd_type in ("BLOCK", "ACTIVATE", "STOP"):
                fo_state["station_served"] = station_id
                if cmd_type in ("BLOCK", "STOP"): fo_state["irrigating"] = False
                add_log("SUCCESS", f"Comando {cmd_type} exitoso en WS-{station_id}")
            elif cmd_type == "GET-STATIONS":
                fo_state["stations"] = resp.get("stations", [])
                add_log("SUCCESS", "Lista de estaciones actualizada.")
    else:
        add_log("ERROR", f"Fallo {cmd_type}: {resp.get('reason')}")

    return jsonify(resp)

@app.route("/api/batch", methods=["POST"])
def api_batch():
    body = request.json or {}
    file_path = body.get("file_path")
    content = body.get("content")

    if file_path:
        commands = read_file(file_path)
    elif content:
        commands = parse_commands_content(content)
    else:
        commands = None
    
    if commands is None:
        add_log("ERROR", "Fallo al procesar el lote.")
        return jsonify({"status": "KO"}), 400

    def _run_batch_bg(cmd_list):
        for st_id, dur in cmd_list:
            resp = send_and_wait_central("IRRIGATION", station_id=st_id, duration=dur)
            if resp.get("status") == "OK":
                with state_lock:
                    fo_state["station_served"] = st_id
                    fo_state["irrigating"] = True
                dur_text = f"{dur}s" if dur else "indefinido"
                add_log("SUCCESS", f"[Lote] WS-{st_id} regando ({dur_text}).")
            else:
                add_log("ERROR", f"[Lote] Fallo en WS-{st_id}")
            time.sleep(2)

    threading.Thread(target=_run_batch_bg, args=(commands,), daemon=True).start()
    return jsonify({"status": "OK"})