import socket
import modules.utils.DBManager as db
import modules.shared.shared_state as st
from modules.kafka_clients import send_event

SEPARATOR = "#"
FORMAT = 'utf-8'

def calculate_lrc(msg: str) -> str:
    """Funcion that calculates the LRC of a message"""
    
    lrc = 0
    
    for byte in msg.encode(FORMAT):
        
        lrc ^= byte
        
    return f"{lrc:02X}"

def check_lrc(msg, recieved_lrc) -> bool:
    
    return calculate_lrc(msg) == str(recieved_lrc)

def send_nap_msg(client_socket:socket.socket, msg: str):
    """Function that calculates, attaches and sends a NAP message"""
    
    lrc_val = calculate_lrc(msg)
    frame = f"{msg}{SEPARATOR}{lrc_val}{SEPARATOR}FIN"
    
    client_socket.sendall(frame.encode(FORMAT))

def alert_operator_serving(station_id: int, reason: str):
    
    operator = db.get_station_serving_operator(station_id)
    
    if operator is None:
        return
    
    msg = {
        
        "operator_id": operator,
        "type": "ALERT",
        "status": "KO",
        "reason": reason,
    }
    
    send_event(topic="central-notis", msg=msg, key=operator)
    db.set_station_serving_operator(station_id, None)

def process_nap_frame(client_socket: socket.socket, fields: list) -> tuple[bool, None | int]:
    """Function that processes a complete and validated NAP message"""
    
    try:
        
        if len(fields) < 4 or fields[0] != "NAP":
            
            send_nap_msg(client_socket, f"NAP#1.0#0#4#0")
            return False, None
        
        ack_code = int(fields[2])
        prot_code = int(fields[3])
        
        # REGISTRATION
        if prot_code == 0:
            
            station_id = int(fields[4])
            station_location = str(fields[5])
            
            if not db.check_station(station_id):
                
                db.store_station(station_id=station_id, station_location=station_location)
                send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#3#{station_id}")
                print(f"[NAP] Station {station_id} ({station_location}) correctly enrolled.")
                st.log(f"WS-{station_id} ({station_location}) correctly enrolled") 
                
            else:
                
                send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#4#1#{station_id}")
                
            return False, station_id
        
        # ACTIVATION      
        elif prot_code == 1:
            
            station_id = int(fields[4])
            
            if db.check_station(station_id):
                
                db.set_station_status(station_id, status="Available")
                send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#3#{station_id}")
                print(f"[NAP] Station {station_id} available and ready for irrigation.")
                st.log(f"WS-{station_id} activated and available") 
                
            else:
                
                send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#4#2#{station_id}")
                
            return False, station_id
        
        # HEALTHCHECKS
        elif prot_code == 2:
            
            station_id = int(fields[4])
            status_msg = str(fields[5])
            
            if db.check_station(station_id):
                
                current_status = db.get_station_status(station_id)
                
                if status_msg == "KO":
                    
                    if current_status != "Leak":
                        
                        db.set_station_status(station_id, "Leak")
                        db.set_station_previous_status(station_id, current_status or "Available")
                        
                        print(f"[NAP] Leak detected in station {station_id}.")
                        st.log(f"WS-{station_id} leak detected")
                        
                        alert_operator_serving(station_id, "LEAK")

                        
                    send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#3#{station_id}")
                    return True, station_id
                
                else:
                    
                    # Reliability purposes. If the station was disconnected but central recieves heartbeat, passes to status "Available"
                    if current_status == "Disconnected": 
                        
                        db.set_station_status(station_id, "Available")
                    
                    if current_status == "Leak":
                        
                        previous_status = db.get_station_previous_status(station_id) or "Available"
                        
                        # When a station recovers from a leak, if it was watering, now it is available
                        if previous_status == "Watering":
                            
                            previous_status = "Available"
                        
                        db.set_station_status(station_id, previous_status)
                        print(f"[NAP] Station {station_id} recovered from Leak. Restored to status '{previous_status}'")
                        st.log(f"WS-{station_id} recovered from leak. Restored to status '{previous_status}'")
                    
                    send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#3#{station_id}")
                    return True, station_id
                
            else:
                
                send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#4#2#{station_id}")
                return False, station_id
        
        elif prot_code in (3, 4):
            
            # This should not happen. In case it happens, central ignores it
            return False, None
            
        else:
            
            send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#4#3")
            return False, None
        
    except Exception as e:
        
        print(f"[NAP ERROR] Fallo procesando trama: {e}")
        return False, None