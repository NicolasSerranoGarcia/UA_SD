"""Container smoke example; Central application logic is not implemented yet."""

import sys
import os
import argparse
import socket
import threading
import sqlite3
from confluent_kafka import Consumer, Producer

FORMAT = 'utf-8'

def manage_WS(client_socket: socket.socket, addr):
    
    msg = client_socket.recv(50).decode(FORMAT).split("#")
    
    while "FIN" not in msg:
        
        msg += client_socket.recv(50).decode(FORMAT).split("#")
    
    end_index = msg.index("FIN")
    rest = msg[end_index+1:]
    msg = msg[:end_index+1 ]
    
    print(msg, rest)
    
    try:
        
        print(msg)
        start_index = msg.index("NAP")
        
        ack_code = int(msg[start_index+2])
        prot_code = int(msg[start_index+3])
        
        print(ack_code, prot_code)
        
        match prot_code:
            
            case 0:
                print("Registration")
            case 1:
                print("Activation")
            case 2:
                print("Status")
            case 3:
                print("ACK")
            case 4:
                print("NACK")
            case _:

                client_socket.send(f"NAP#1.0#{ack_code}#4#3#FIN".encode(FORMAT))
        
    except ValueError:
        
        print("NAP starter couldn´t be found")
        
    finally:
        
        client_socket.close()
        
        



def main():
    
    argparser = argparse.ArgumentParser(description="Central for Water Management Network")
    
    argparser.add_argument("--port", type=int, default=int(os.getenv("PORT", 8080)), help="Listening port of the socket server")
    argparser.add_argument("--kafka-ip", type=str, default=os.getenv("KAFKA_IP", "iriguchi.proxy.rlwy.net"), help="IP of the kafka bootstrap server")
    argparser.add_argument("--kafka-port", type=int, default=int(os.getenv("KAFKA_PORT", 37040)),help="Port of the kafka bootstrap server")
    
    args = argparser.parse_args()
    
    server_ip = "127.0.0.1"
    server_port = args.port
    kafka_ip = args.kafka_ip
    kafka_port = args.kafka_port
    
    validation_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
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
        
        print("Interruption forced. Exiting the program...")
    
    except Exception as e:
        
        print("Unexpected error:", e)
        
    finally:
        
        validation_server.close()


if __name__ == "__main__":
    main()

