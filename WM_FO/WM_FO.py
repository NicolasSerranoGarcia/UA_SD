import argparse
import time
import os
import subprocess
import json
from confluent_kafka import Consumer, Producer, KafkaException

FORMAT = 'utf-8'

def print_menu():
    
    print("1.Start Irrigation")
    print("2.Block station")
    print("3.Activate station")
    print("4.Show stations")
    print("5.Read requests from file")
    print("6.Exit")

def flush_cmd():
    
    command = "clear" if os.name != "nt" else "cls"
    subprocess.run(command, shell=True)

def read_file(file_path: str) -> list:

    
    try:
        
        with open(file_path, mode="r", encoding="utf-8") as file:
            
            raw_lines = file.readlines()
            commands = []
            for line in raw_lines:
                
                command = line.strip()
                
                if not command:
                    continue
                
                command = command.split("#")
                
                if len(command) >= 2:
                    command[0] = int(command[0])
                    command[1] = int(command[1])
                    commands.append(tuple(command))
                
                else:
                    commands.append(int(line))
                
                
    except OSError:
        
        print(f"File '{file_path}' does not exists. Try again")
        return None
                  
            
    return commands
 
 
def send_command(producer: Producer, station_id: int, type: str, duration: int = 120):
    
    msg = {"operator_id": operator_id, 
           "station_id": station_id, 
           "command_type": type,
           "duration": duration}
    
    producer.produce(topic="FO-Req", value=json.dumps(msg).encode(FORMAT))
    producer.flush()   
    
def main():
    
    argparser = argparse.ArgumentParser(description="Field Operator for Water Management Network")
    
    argparser.add_argument("ID", type=int, help="Unique ID of the field operator")
    argparser.add_argument("--kafka-ip", type=str, default="iriguchi.proxy.rlwy.net", help="IP of the kafka bootstrap server")
    argparser.add_argument("--kafka-port", type=int, default=37040, help="Port of the kafka bootstrap server")
    
    args = argparser.parse_args()

    global operator_id; operator_id = args.ID
    
    kafka_config = {
    "bootstrap.servers": f"{args.kafka_ip}:{args.kafka_port}",

        "log_cb": lambda *args: None,  
        "error_cb": lambda err: None, 
        "log_level": 0,
        "socket.timeout.ms": 2000,
        "message.timeout.ms": 3000,
    }   
    
    try:
        
        producer = Producer(kafka_config)

    except KafkaException as e:
        
        producer = None
      
    print_menu()
    try:    
        input_option = int(input("Select an option: "))
        
        while input_option != 6:
            
            match input_option:
                
                case 1:
                    
                    station_id = input("Select station to irrigate (q to exit): ")
                    
                    if station_id != 'q':
                        
                        duration = input("Specify duration of the irrigation (q to exit / empty for default (120s)): ")
                        
                        if duration != 'q':
                            
                            duration = int(duration) if duration else 120
                            send_command(producer=producer, station_id=int(station_id), type="IRRIGATION", duration=duration)
                    
                case 2:
                    
                    station_id = input("Select station to block (q to exit): ")
                    
                    if station_id != 'q':
                        
                        send_command(producer=producer, station_id=int(station_id), type="BLOCK")
                    
                case 3  :
                    
                    station_id = input("Select station to activate (q to exit): ")
                    
                    if station_id != 'q':
                        
                        send_command(producer=producer, station_id=int(station_id), type="ACTIVATE")
                    
                case 4:
                    print("Showing stations...")
                    
                case 5:
                    
                    while True:
                        
                        file_path = input("Enter the path of the file ('q' to cancel): ").strip()
                        
                        if file_path == 'q':
                            
                            break
                        
                        commands = read_file(file_path=file_path)
                        
                        if commands is not None:
                            
                            for command in commands:
                                
                                if type(command) == tuple:
                                    
                                    send_command(producer=producer, station_id=command[0], duration=command[1], type="IRRIGATION")
                                    
                                else:
                                    
                                    send_command(producer=producer, station_id=command, type="IRRIGATION")
                                    
                                time.sleep(4)
                                    
                            break

                case _:
                    print("Invalid option")
            
            print_menu()
            input_option = int(input("Select an option: "))
    
    except KeyboardInterrupt:
        print("\nInterrupted. Exiting...")
        exit(1)
    
    print("\nExiting the aplication. Come back soon.")
    exit(0)

if __name__ == "__main__":
    main()
