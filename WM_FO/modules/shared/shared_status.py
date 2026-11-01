import threading
import queue
import time

central_notis_queue = queue.Queue()

state_lock = threading.RLock()
fo_state = {
    "operator_id": None,
    "station_served": None,
    "irrigating": False,
    "current_flux": None,
    "accumulated_volume": 0.0,
    "last_telemetry_ts": None,
    "last_summary": None,
    "stations": [],
    "logs": []
}

def add_log(level: str, message: str):
    """Adds a message to the Web UI log and prints it to the console."""
    ts = time.strftime("%H:%M:%S")
    entry = {"time": ts, "level": level, "message": message}
    print(f"[{ts}] [{level}] {message}")
    with state_lock:
        fo_state["logs"].insert(0, entry)
        if len(fo_state["logs"]) > 50:
            fo_state["logs"].pop()