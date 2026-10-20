import socket
import modules.DBManager as db

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
                
            else:
                
                send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#4#2#{station_id}")
                
            return False, station_id
        
        # HEALTHCHECKS
        elif prot_code == 2:
            
            station_id = int(fields[4])
            status_msg = str(fields[5])
            
            if db.check_station(station_id):
                
                if status_msg == "KO":
                    
                    db.set_station_status(station_id, "Leak")
                    send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#3#{station_id}")
                    f"[NAP] Leak detected in station {station_id}."
                    return False, station_id
                
                else:
                    
                    db.set_station_status(station_id, "Available")
                    send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#3#{station_id}")
                    return True, station_id
                
            else:
                
                send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#4#2#{station_id}")
                return False, station_id
        
        elif prot_code == 3:
            
            # TODO
            return False, None
            
        elif prot_code == 4:
            
            # TODO
            return False, None
            
        else:
            
            send_nap_msg(client_socket, f"NAP#1.0#{ack_code}#4#3")
            return False, None
        
    except Exception as e:
        
        print(f"[NAP ERROR] Fallo procesando trama: {e}")
        return False, None