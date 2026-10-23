import argparse
import time
import os
import subprocess
import json
from confluent_kafka import Consumer, Producer, KafkaException

FORMAT = 'utf-8'
DEFAULT_DURATION = 120

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

def ask_int(cmd_msg: str, error_msg: str, default: int | None = None) -> int | None:
    """Function that asks for an int in the terminal and returns the int if the input is correct, None
    if the input is 'q' to exit. This is done until the input is correct"""
    
    while True:
        
        introduced_value = input(cmd_msg).strip()
        
        if introduced_value.lower() == "q":
            return None
        
        if not introduced_value and default is not None:
            return default
        
        if introduced_value.isdigit():
            return int(introduced_value)
        
        print(error_msg)
    

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
        
        print(f"[ERROR] File '{file_path}' does not exists. Try again")
        return None
                  
            
    return commands
 
 
def send_command(producer: Producer, station_id: int, type: str, duration: int = DEFAULT_DURATION):
    
    if producer is None:
        print("[ERROR] Kafka connection failed. Command omitted.")
        return
    
    msg = {"operator_id": operator_id, 
           "station_id": station_id, 
           "command_type": type,
           "duration": duration}
    
    producer.produce(topic="fo-req", value=json.dumps(msg).encode(FORMAT))
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

                
                # ACTIVATE A STATION    
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
                
                # BLOCK A STATION      
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
                    print("Showing stations...")
                    
                
                # READ COMMANDS FROM A FILE 
                case '5':
                    
                    while True:
                        
                        file_path = input("Enter the path of the file ('q' to cancel): ").strip()
                        
                        if file_path == 'q':
                            
                            time.sleep(1)
                            flush_cmd()
                            break
                        
                        commands = read_file(file_path=file_path)
                        
                        if commands is not None:
                            
                            for command in commands:
                                
                                if isinstance(command, tuple):
                                    
                                    send_command(producer=producer, station_id=command[0], duration=command[1], type="IRRIGATION")
                                    
                                else:
                                    
                                    send_command(producer=producer, station_id=command, type="IRRIGATION")
                                    
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
