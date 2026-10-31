import sys
import os
import argparse
import socket
import threading
import modules.utils.DBManager as db
import modules.utils.NAPManager as nap 
import modules.shared.shared_state as st   
import modules.kafka_clients as kcli
from modules.shared.shared_state import operation_fo_dict as _operation_fo_dict
from modules.shared.shared_state import operation_central_dict as _operation_central_dict
from modules.web.web_api import start_web

FORMAT = 'utf-8'
BLOCK_TAM = 1024
SEPARATOR = "#"
STATION_POSSIBLE_STATUS = ["Available", "Watering", "Disconnected", "Leak", "Out of service"]
COMMAND_STATUS_TRADUCTION = {
    
    "IRRIGATION": "Watering",
    "BLOCK": "Out of service",
    "ACTIVATE": "Available",
    "STOP": "Available",
}

def _station_lost(station_id: int, reason: str):
    
    print(f"[NAP] Connection lost with station WS-{station_id}: {reason}")
    db.set_station_status(station_id, "Disconnected")
    nap.alert_operator_serving(station_id, "DISCONNECTED")
    st.log(f"[CONNECTION LOST] WS-{station_id} disconnected: {reason}")

def manage_WS(client_socket: socket.socket, addr):
    """Function that manages the connection through sockets with an station"""
    
    buffer = ""
    current_station_id = None
    keep_connection = False
    lost_reason = None
    
    client_socket.settimeout(3.0)
    
    try:
        
        while True:
            
            try:
                
                block = client_socket.recv(BLOCK_TAM).decode(FORMAT)
                
                if not block:
                    
                    if current_station_id:
                        
                        lost_reason = "connection closed"
                        
                    break
                
                buffer += block
                
                while f"{SEPARATOR}FIN" in buffer:
                    
                    fin_idx = buffer.find(f"{SEPARATOR}FIN")
                    frame = buffer[:fin_idx]
                    buffer = buffer[fin_idx + len(SEPARATOR) + 3:]
                    
                    fields = frame.split(SEPARATOR)
                    
                    if not nap.check_lrc(msg=SEPARATOR.join(fields[:-1]), recieved_lrc=fields[-1]):
                        
                        ack_code = fields[2]
                        nap.send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#4#0")
                        continue
                        
                    keep_connection, station_id = nap.process_nap_frame(client_socket, fields[:-1])
                    
                    if station_id:
                        current_station_id = station_id
                        
                    if not keep_connection:
                        return
                        
                        
                
            except socket.timeout:
                
                lost_reason = "hearbeat timeout"
                    
                break
        
    except Exception as e:
        
        print(f"[Socket Thread Error] {addr}: {e}")
        lost_reason = f"Socket error {e}"
        
    finally:
           
        client_socket.close()
        if lost_reason is not None and keep_connection and current_station_id is not None:
            
            _station_lost(current_station_id, lost_reason)

def telemetry_handler(data: dict):
    
    station_id = data.get("station_id")
    flux = data.get("flux_l_min")
    
    status = db.get_station_status(station_id)
    
    if status == "Watering":
        
        st.update_telemetry(station_id, flux)
        print(f"[IRRIGATING] Station {station_id:<6} Flux: {flux}")

def fo_req_handler(data:dict):
    
    operator_id = data.get("operator_id")
    station_id = data.get("station_id")
    command = data.get("command_type")
    operation_code = data.get("operation_code")
    
    # First if the operator does not exists in the DB we store it
    if not db.check_operator(operator_id):
        
        db.store_operator(operator_id)
    
    # If the station does not exist, we notify de FO
    if command != "GET-STATIONS" and not db.check_station(station_id):
        
        msg = {
            "operator_id": operator_id,
            "type": "RESPONSE",
            "operation_code": operation_code,
            "status": "KO",
            "reason": f"Sation {station_id} does not exist in the DB"
        }
        
        kcli.send_event(topic="central-notis", msg=msg, key=operator_id)
        
        return
    
    match command:
        
        case "IRRIGATION":
            
            station_served = db.get_operator_station_served(operator_id)
            
            # This is done so an operator cannot serve two stations at the same time
            if station_served is not None and station_served != station_id:
                
                target_topic = "central-notis"
                key = operator_id
                
                msg = {
                    "operator_id": operator_id,
                    "type": "RESPONSE",
                    "operation_code": operation_code,
                    "status": "KO",
                    "reason": f"You cannot serve another station, you are currently serving station WS-{station_served}",
                }
                
                st.log(f"FO-{operator_id} {command} on WS-{station_id} DENIED: operator is already serving an station")
                kcli.send_event(topic=target_topic, msg=msg, key=key)
                return
            
            # If the operator is not serving an station or is the same station is serving
            station_status = db.get_station_status(station_id)
            
            if station_status == "Available":
                
                target_topic = "central-ops"
                key = station_id
                duration = data.get("duration")
                
                msg = {
                    "operator_id": operator_id,
                    "station_id": station_id,
                    "type": command,
                    "operation_code": operation_code,
                    "duration": duration,
                }
                
                # Central notifies the FO about the process
                kcli.send_event(topic="central-notis", key=operator_id, msg={
                    "operator_id": operator_id, "type": "INFO", "operation_code": operation_code,
                    "message": f"Resquest of command {command} for WS-{station_id} sent correctly, waiting for response"})
                
                _operation_fo_dict[operation_code] = (operator_id, command)    
                st.log(f"FO-{operator_id} requested {command} on WS-{station_id}")          
            
            elif station_status in STATION_POSSIBLE_STATUS:
                
                target_topic = "central-notis"
                key = operator_id
                reason = (f"Station WS-{station_id} is already irrigating" if station_status == "Watering" 
                        else f"Station WS-{station_id} is in status {station_status} so it cannot start an irrigation")
                
                msg = {
                    "operator_id": operator_id,
                    "type": "RESPONSE",
                    "operation_code": operation_code,
                    "status": "KO",
                    "reason": reason,
                }

                st.log(f"FO-{operator_id} {command} on WS-{station_id} DENIED: {reason}")
                
            else:
                
                print(f"[STATUS ERROR] Unexpected status of station {station_id} in DB: {station_status}")
                st.log(f"[STATUS ERROR] Unexpected status of station WS-{station_id} in DB: {station_status}")
                return
            
            kcli.send_event(topic=target_topic, msg=msg, key=key)
            
        case "BLOCK":
            
            station_status = db.get_station_status(station_id)
            
            if station_status == "Available" or station_status == "Watering":
                
                target_topic = "central-ops"
                key = station_id
                
                msg = {
                    "operator_id": operator_id,
                    "station_id": station_id,
                    "type": command,
                    "operation_code": operation_code,
                }
                
                # Central notifies the FO about the process
                kcli.send_event(topic="central-notis", key=operator_id, msg={
                    "operator_id": operator_id, "type": "INFO", "operation_code": operation_code,
                    "message": f"Resquest of command {command} for WS-{station_id} sent correctly, waiting for response"})
                
                _operation_fo_dict[operation_code] = (operator_id, command)
                st.log(f"FO-{operator_id} requested {command} on WS-{station_id}")   
            
            
            elif station_status in STATION_POSSIBLE_STATUS:
                
                target_topic = "central-notis"
                key = operator_id
                reason = (f"Station WS-{station_id} is already blocked" if station_status == "Out of service" 
                        else f"Station WS-{station_id} is in status {station_status} so it cannot be blocked")
                
                msg = {
                    "operator_id": operator_id,
                    "type": "RESPONSE",
                    "operation_code": operation_code,
                    "status": "KO",
                    "reason": reason,
                }
                
                st.log(f"FO-{operator_id} {command} on WS-{station_id} DENIED: {reason}")
             
            else:
                
                print(f"[STATUS ERROR] Unexpected status of station {station_id} in DB: {station_status}")
                return
            
            kcli.send_event(topic=target_topic, msg=msg, key=key)
            
        case "ACTIVATE":
            
            station_status = db.get_station_status(station_id)
            
            if station_status == "Out of service":
                
                target_topic = "central-ops"
                key = station_id
                
                msg = {
                    "operator_id": operator_id,
                    "station_id": station_id,
                    "type": command,
                    "operation_code": operation_code,
                }
                
                # Central notifies the FO about the process
                kcli.send_event(topic="central-notis", key=operator_id, msg={
                    "operator_id": operator_id, "type": "INFO", "operation_code": operation_code,
                    "message": f"Resquest of command {command} for WS-{station_id} sent correctly, waiting for response"})
                
                _operation_fo_dict[operation_code] = (operator_id, command)
                st.log(f"FO-{operator_id} requested {command} on WS-{station_id}")   
            
            
            elif station_status in STATION_POSSIBLE_STATUS:
                
                target_topic = "central-notis"
                key = operator_id
                reason = (f"Station WS-{station_id} is already activated" + (" and watering" if station_status == "Watering" else "") 
                        if station_status in ["Available", "Watering"] 
                        else f"Station WS-{station_id} is in status {station_status} so it cannot be activated")
                
                msg = {
                    "operator_id": operator_id,
                    "type": "RESPONSE",
                    "operation_code": operation_code,
                    "status": "KO",
                    "reason": reason,
                }
             
                st.log(f"FO-{operator_id} {command} on WS-{station_id} DENIED: {reason}")
             
            else:
                
                print(f"[STATUS ERROR] Unexpected status of station {station_id} in DB: {station_status}")
                st.log(f"[STATUS ERROR] Unexpected status of station WS-{station_id} in DB: {station_status}")
                return
            
            kcli.send_event(topic=target_topic, msg=msg, key=key)
        
        case "STOP":
          
            station_status = db.get_station_status(station_id)
            
            if station_status == "Watering":
                
                target_topic = "central-ops"
                key = station_id
                
                msg = {
                    "operator_id": operator_id,
                    "station_id": station_id,
                    "type": command,
                    "operation_code": operation_code,
                }
            
                # Central notifies the FO about the process
                kcli.send_event(topic="central-notis", key=operator_id, msg={
                    "operator_id": operator_id, "type": "INFO", "operation_code": operation_code,
                    "message": f"Resquest of command {command} for WS-{station_id} sent correctly, waiting for response"})
            
                _operation_fo_dict[operation_code] = (operator_id, command)
                st.log(f"FO-{operator_id} requested {command} on WS-{station_id}")   
            
            elif station_status in STATION_POSSIBLE_STATUS:
                
                target_topic = "central-notis"
                key = operator_id
                reason = f"Stations can only be stoped when they are watering. Station {station_id} is currently in status {station_status}"
                
                msg = {
                    "operator_id": operator_id,
                    "type": "RESPONSE",
                    "operation_code": operation_code,
                    "status": "KO",
                    "reason": reason,
                }
                
                st.log(f"FO-{operator_id} {command} on WS-{station_id} DENIED: {reason}")
             
            else:
                
                print(f"[STATUS ERROR] Unexpected status of station {station_id} in DB: {station_status}")
                st.log(f"[STATUS ERROR] Unexpected status of station WS-{station_id} in DB: {station_status}")
                return
            
            kcli.send_event(topic=target_topic, msg=msg, key=key)  
        
        case "GET-STATIONS":

            target_topic = "central-notis"
            key = operator_id
            stations: list = db.read_all_stations()
            
            msg = {
                "operator_id":operator_id,
                "type":"RESPONSE",
                "operation_code":operation_code,
                "status":"OK",
                "stations":stations,
            }
            
            kcli.send_event(topic=target_topic, msg=msg, key=key)

        case _:
            
            print("[FO ERROR] Invalid operation received")
            st.log(f"[FO ERROR] Invalid operation received from operator FO-{operator_id}")
    
def ws_finish_conf_handler(data: dict):
    
    station_id = data.get("station_id")
    status = data.get("status")
    total_time = data.get("total_time")
    total_liters = data.get("total_liters")
    
    if total_time == 0 or total_time is None:
        
        print(f"[IRRIGATING ERROR] Total watering time cannot be 0. Station WS-{station_id}")
        
        if db.get_station_status(station_id) == "Watering":
        
            db.set_station_status(station_id, "Available")
            
        db.set_station_serving_operator(station_id, None)
        return
    
    if status == "FINISHED":
        
        if db.get_station_status(station_id) == "Watering":
        
            db.set_station_status(station_id, "Available")
            
        st.log(f"[IRRIGATION FINISHED] WS-{station_id} finished: {total_liters} L in {total_time} min. Average flux: {total_liters/total_time} L/min")
        
        operator_id = data.get("operator_id") or db.get_station_serving_operator(station_id)
        db.set_station_serving_operator(station_id, None)
        
        if operator_id is not None: # Send the operator the summary
            
            target_topic = "central-notis"
            summary = (station_id, total_time, total_liters)
            
            msg = {
                "operator_id": operator_id,
                "type":"SUMMARY",
                "status":"OK",
                "summary":summary,
            }
            
            kcli.send_event(topic=target_topic, msg=msg, key=operator_id)
        
def ws_start_conf_handler(data: dict):
    
    station_id = data.get("station_id")
    
    operation_code = data.get("operation_code")
    
    status = data.get("status")
    
    if operation_code is None: # Irrigation started by the station itself
        
        if status == "OK":
            
            db.set_station_status(station_id, "Watering")
            st.reset_telemetry(station_id)
            print(f"[IRRIGATION STARTED] Station WS-{station_id} started irrigating")
            st.log(f"[IRRIGATION STARTED] ]WS-{station_id} started irrigating (manual)")
    
        
    else: # Confirmation of an operation sent by the central or by a field operator
        
        if operation_code in _operation_central_dict: # The operation was sent by central
            
            command = _operation_central_dict.pop(operation_code)
            
            if status == "OK":
                
                print(f"[COMMAND SUCCESS] Success in command {command} sent to station WS-{station_id}")
                db_status = COMMAND_STATUS_TRADUCTION.get(command)
                db.set_station_status(station_id, db_status)
                
                if command == "IRRIGATION":
                    st.reset_telemetry(station_id)
                
                if command in ("BLOCK", "STOP"):
                    nap.alert_operator_serving(station_id, command, message=f"Central bloqued station WS-{station_id}")
                
            elif status == "KO":
                
                print(f"[COMMAND FAILURE] Error in station WS-{station_id} trying command {command}: Reason: {data.get("reason")}")
                
            st.log(f"[WS RESPONSE] Central -> WS-{station_id}. Command: {command}. Response: {status}. " + (f"Reason: {data.get("reason")}" if status == "KO" else ""))
             
        elif operation_code in _operation_fo_dict: # The operation was sent by a field operator
            
            operator_id, command = _operation_fo_dict.pop(operation_code)
            
            if status == "OK":
                
                print(f"[COMMAND SUCCESS] Success in command {command} sent to station WS-{station_id} by operator FO-{operator_id}")
                db_status = COMMAND_STATUS_TRADUCTION.get(command)
                db.set_station_status(station_id, db_status)
                
                if command == "IRRIGATION":
                    
                    db.set_station_serving_operator(station_id, operator_id)
                    st.reset_telemetry(station_id)
                    
                elif command in ("STOP", "BLOCK"):
                    
                    actual_serving = db.get_station_serving_operator(station_id)
                    if actual_serving is not None and actual_serving != operator_id:
                        
                        nap.alert_operator_serving(station_id, reason=command, message=f"Station serving was {command} by FO-{operator_id}")
                    
                    else:
                        db.set_station_serving_operator(station_id, None)
                
            elif status == "KO":
               
               print(f"[COMMAND FAILURE] Error in station WS-{station_id} trying command {command} by operator FO-{operator_id}: Reason: {data.get("reason")}")
                
            msg = {
                "operator_id": operator_id,
                "type": "RESPONSE",
                "operation_code": operation_code,
                "status": status,
                "reason": data.get("reason"),
            }
        
            kcli.send_event(topic="central-notis", msg=msg, key=operator_id)
            st.log(f"WS-{station_id} {command} by FO-{operator_id}: {status} ." + (f"Reason: {data.get("reason")}" if status == "KO" else ""))
 
        else:
            
            print(f"[UNEXPECTED ERROR] Operation code {operation_code} sent by station WS-{station_id} not recognized")
    
def main():
    
    argparser = argparse.ArgumentParser(description="Central for Water Management Network")
    
    argparser.add_argument("--server-ip", type=str, default=os.getenv("SERVER_IP", "0.0.0.0"), help="Listening ip of the socket server")
    argparser.add_argument("--port", type=int, default=int(os.getenv("WS_PORT", 8080)), help="Listening port of the socket server")
    argparser.add_argument("--kafka-ip", type=str, default=os.getenv("KAFKA_IP", "iriguchi.proxy.rlwy.net"), help="IP of the kafka bootstrap server")
    argparser.add_argument("--kafka-port", type=int, default=int(os.getenv("KAFKA_PORT", 37040)),help="Port of the kafka bootstrap server")
    
    args = argparser.parse_args()
    
    server_ip = args.server_ip
    server_port = args.port
    kafka_ip = args.kafka_ip
    kafka_port = args.kafka_port
    
    bootstrap_servers = f"{kafka_ip}:{kafka_port}"
    
    validation_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    validation_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1) # Avoids OSError: [Errno 98] Address already in use
    validation_server.bind((server_ip, server_port))
    validation_server.settimeout(1.0)
    validation_server.listen()
    
    print(f"Server listening through {server_ip}:{server_port}")
    
    stop_event = threading.Event()
    topic_handlers = {
        "telemetry": telemetry_handler,
        "fo-req": fo_req_handler,
        "ws-finish-conf": ws_finish_conf_handler,
        "ws-start-conf": ws_start_conf_handler,
    }
    
    try:
        
        kcli.start_kafka(bootstrap_servers, stop_event, topic_handlers)
    
    except Exception as e:
        
        print(f"[KAFKA ERROR] {e}")
        return 
    
    start_web(int(os.getenv("WEB_PORT", 8000)))
    
    # Sockets management
    try:
    
        while True:
            
            try:
            
                client_socket, client_addr = validation_server.accept()
                
                managing_thread = threading.Thread(target=manage_WS, args=(client_socket, client_addr), daemon=True)
                managing_thread.start()
                
            except socket.timeout:
                
                continue
            
    except KeyboardInterrupt:
        
        print("Interruption forced. Exiting the program...")
    
    except Exception as e:
        
        print("Unexpected error:", e)
        
    finally:
        
        kcli.stop_kafka()
        validation_server.close()


if __name__ == "__main__":
    
    db.init_database()
    db.reset_stations()
    main()
    db.stop_database()

