import argparse
import time
import os
import json
from confluent_kafka import Consumer, Producer, KafkaException
from modules.utils import *

FORMAT = 'utf-8'
DEFAULT_DURATION = 120

def send_command(producer: Producer, station_id: int | None, type: str, duration: int):
    
    if producer is None:
        print("[ERROR] Producer is not created. Command omitted.")
        return
    
    msg = {"operator_id": operator_id, 
           "station_id": station_id, 
           "command_type": type,
           "duration": duration}
    try: 
        producer.produce(topic="fo-req", value=json.dumps(msg).encode(FORMAT))
        producer.flush()
        
    except Exception as e:
        
        print(f"[ERROR] Failed to send message to Kafka: {e}")
    
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
        
        print(e)
        producer = None
      
    try:
        
        while True:
            
            print_menu()
            input_option = input("Select an option: ").strip()
            
            if input_option == '6':
                
                print("\nExiting application. Come back soon.")
                break
            
            match input_option:
                
                # START IRRIGATION
                case '1':
                    
                    station_id = ask_int(
                        cmd_msg="Select station to start the irrigation ('q' to cancel): ",
                        error_msg="[ERROR] Please introduce a correct station ID"
                    )

                    if station_id is None:

                        time.sleep(1)
                        flush_cmd()
                        continue
                    
                    duration = ask_int(
                        cmd_msg=f"Specify duration ('q' to cancel / empty for default ({DEFAULT_DURATION}s)): ",
                        error_msg="[ERROR] Please specify a correct duration",
                        default=DEFAULT_DURATION
                    )
                    
                    if duration is None:
                        
                        time.sleep(1)
                        flush_cmd()
                        continue
    
                    send_command(producer=producer, station_id=station_id, type="IRRIGATION", duration=duration)

                
                # BLOCK A STATION    
                case '2':
                    
                    station_id = ask_int(
                        cmd_msg="Select station to block ('q' to cancel): ",
                        error_msg="[ERROR] Please introduce a correct station ID"
                    )
                    
                    if station_id is None:
                        
                        time.sleep(1)
                        flush_cmd()
                        continue
                        
                    send_command(producer=producer, station_id=station_id, type="BLOCK")  
                
                # ACTIVATE A STATION      
                case '3':
                    
                    station_id = ask_int(
                        cmd_msg="Select station to activate ('q' to cancel): ",
                        error_msg="[ERROR] Please introduce a correct station ID"
                    )
                    
                    if station_id is None:
                        
                        time.sleep(1)
                        flush_cmd()
                        continue
                        
                    send_command(producer=producer, station_id=station_id, type="ACTIVATE")  
                
                # SHOW ALL THE STATIONS   
                case '4':
                    
                    send_command(producer=producer, station_id=None, type="REQUEST-STATIONS")
                    
                
                # READ COMMANDS FROM A FILE 
                case '5':
                    
                    while True:
                        
                        file_path = input("Enter the path of the file ('q' to cancel): ").strip()
                        
                        if file_path == 'q':
                            
                            time.sleep(1)
                            flush_cmd()
                            break
                        
                        commands = read_file(file_path=file_path)
                        print(commands)
                        
                        if commands is not None:
                            
                            for command in commands:
                                
                                if isinstance(command, tuple):
                                    
                                    send_command(producer=producer, station_id=command[0], duration=command[1], type="IRRIGATION")
                                    
                                else:
                                    
                                    send_command(producer=producer, station_id=command, duration=DEFAULT_DURATION, type="IRRIGATION")
                                    
                                time.sleep(4)
                                    
                            break
                
                # INCORRECT OPTION
                case _:
                    print("[ERROR]: Invalid option. Please select a correct option.")
                    time.sleep(1)
                    flush_cmd()
    
    except KeyboardInterrupt:
        print("\nInterrupted. Exiting...")
        exit(1)
    
    exit(0)

if __name__ == "__main__":
    main()
