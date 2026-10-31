import threading
import time
from collections import deque
from datetime import datetime

operation_fo_dict: dict = {}        # operation_code -> (operator_id, command)
operation_central_dict: dict = {}   # operation_code -> command
telemetry: dict = {}                # station_id -> (flux, volume, ts)

_events = deque(maxlen=100)
_lock = threading.Lock()

def log(text: str):
    with _lock:
        _events.appendleft(f"{datetime.now():%H:%M:%S} {text}")

def get_events() -> list:
    with _lock:
        return list(_events)
    
def update_telemetry(station_id, flux):
    now = time.time()
    flux = float(flux or 0)
    t = telemetry.get(station_id)
    volume = t["volume"] + flux * (now - t["ts"]) / 60 if t else 0.0
    telemetry[station_id] = {"flux": flux, "volume": volume, "ts": now}

def reset_telemetry(station_id):
    telemetry.pop(station_id, None)
