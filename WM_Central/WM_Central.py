import sys
import os
import argparse
import socket
import threading
import modules.DBManager as db
import modules.NAPManager as nap
from confluent_kafka import Consumer, Producer

FORMAT = 'utf-8'
BLOCK_TAM = 1024
SEPARATOR = "#"
                             
def manage_WS(client_socket: socket.socket, addr):
    
    buffer = ""
    current_station_id = None
    keep_connection = False
    
    client_socket.settimeout(3.0)
    
    try:
        
        while True:
            
            try:
                
                block = client_socket.recv(BLOCK_TAM).decode(FORMAT)
                
                if not block:
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
                
                if keep_connection and current_station_id is not None:
                    
                    print(f"[NAP TIMEOUT] Socket timeout expired. Connection lost with station {current_station_id}")
                    db.set_station_status(current_station_id, status="Disconnected")
        
    except Exception as e:
        
        print(f"[Socket Thread Error] {addr}: {e}")
        
    finally:
        
        client_socket.close()
        



def main():
    
    argparser = argparse.ArgumentParser(description="Central for Water Management Network")
    
    argparser.add_argument("--server-ip", type=str, default=os.getenv("SERVER_IP", "0.0.0.0"), help="Listening ip of the socket server")
    argparser.add_argument("--port", type=int, default=int(os.getenv("PORT", 8080)), help="Listening port of the socket server")
    argparser.add_argument("--kafka-ip", type=str, default=os.getenv("KAFKA_IP", "iriguchi.proxy.rlwy.net"), help="IP of the kafka bootstrap server")
    argparser.add_argument("--kafka-port", type=int, default=int(os.getenv("KAFKA_PORT", 37040)),help="Port of the kafka bootstrap server")
    
    args = argparser.parse_args()
    
    server_ip = args.server_ip
    server_port = args.port
    kafka_ip = args.kafka_ip
    kafka_port = args.kafka_port
    
    validation_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    validation_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1) # Avoids OSError: [Errno 98] Address already in use
    validation_server.bind((server_ip, server_port))
    validation_server.settimeout(1.0)
    validation_server.listen()
    
    print(f"Server listening through {server_ip}:{server_port}")
    
    try:
    
        while True:
            
            try:
            
                client_socket, client_addr = validation_server.accept()
                
                managing_thread = threading.Thread(target=manage_WS, args=(client_socket, client_addr), daemon=True)
                managing_thread.start()
                
            except socket.timeout:
                
                continue
            
    except KeyboardInterrupt:
        
        db.stop_database()
        print("Interruption forced. Exiting the program...")
    
    except Exception as e:
        
        db.stop_database()
        print("Unexpected error:", e)
        
    finally:
        
        validation_server.close()


if __name__ == "__main__":
    
    db.init_database()
    main()

