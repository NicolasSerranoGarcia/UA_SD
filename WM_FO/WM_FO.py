import argparse
import time
import queue
from confluent_kafka import Consumer, Producer, KafkaException
from modules.utils import *
from modules.kafka_clients import *

DEFAULT_DURATION = 120

def main():
    
    argparser = argparse.ArgumentParser(description="Field Operator for Water Management Network")
    
    argparser.add_argument("ID", type=int, help="Unique ID of the field operator")
    argparser.add_argument("--kafka-ip", type=str, default="iriguchi.proxy.rlwy.net", help="IP of the kafka bootstrap server")
    argparser.add_argument("--kafka-port", type=int, default=37040, help="Port of the kafka bootstrap server")
    
    args = argparser.parse_args()

    global operator_id; operator_id = args.ID
    
    kafka_config = {
        "bootstrap.servers": f"{args.kafka_ip}:{args.kafka_port}",
        "socket.timeout.ms": 2000,
        "message.timeout.ms": 3000,
    }   
    
    try:
        
        producer = Producer(kafka_config)
        
        stop_event = threading.Event()
        start_consumers(
            bootstrap_servers=f"{args.kafka_ip}:{args.kafka_port}",
            operator_id=operator_id,
            stop_event=stop_event
        )

    except KafkaException as e:
        
        print(f"[KAFKA ERROR] Error creating producer: {e}")
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
    
                    send_command(producer=producer, operator_id=operator_id, station_id=station_id, type="IRRIGATION", duration=duration)
                    
                    

                
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
                        
                    send_command(producer=producer, operator_id=operator_id, station_id=station_id, type="BLOCK")
                    print("Waiting for the central to respond...")
                    
                    time_out = 5.0
                    start_time = time.time()
                    response_found = False
                    
                    while time.time() - start_time < time_out:
                        
                        try:
                        
                            response: dict = response_queue.get(timeout=0.5)
                            
                            if response.get("station_id") == station_id and response.get("type") == "BLOCK":
                                
                                response_found = True
                                print("Message recieved from central: ", endl="")
                                
                                if response.get("result") == "OK":
                                    print(f"Station {station_id} blocked successfully")
                                    
                                elif response.get("result") == "KO":
                                    print(f"Failed blocking station {station_id}. Reason: {response.get("error_code")}")
                                    
                                break
                        
                        except queue.Empty:
                            
                            continue
                        
                    if not response_found:
                        
                        print("[CENTRAL ERROR] Timeout reached: central did not send a respond in time")
                        
                    time.sleep(2)
                    flush_cmd()
                        
                
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
                        
                    send_command(producer=producer, operator_id=operator_id, station_id=station_id, type="ACTIVATE")  
                    print("Waiting for the central to respond...")
                    
                    time_out = 5.0
                    start_time = time.time()
                    response_found = False
                    
                    while time.time() - start_time < time_out:
                        
                        try:
                        
                            response: dict = response_queue.get(timeout=0.5)
                            
                            if response.get("station_id") == station_id and response.get("type") == "ACTIVATE":
                                
                                response_found = True
                                print("Message recieved from central: ", endl="")
                                
                                if response.get("result") == "OK":
                                    print(f"Station {station_id} activated successfully")
                                    
                                elif response.get("result") == "KO":
                                    print(f"Failed activating station {station_id}. Reason: {response.get("error_code")}")
                                    
                                break
                        
                        except queue.Empty:
                            
                            continue
                        
                    if not response_found:
                        
                        print("[CENTRAL ERROR] Timeout reached: central did not send a respond in time")
                        
                    time.sleep(2)
                    flush_cmd()

  
                # SHOW ALL THE STATIONS   
                case '4':
                    
                    send_command(producer=producer, operator_id=operator_id, station_id=None, type="GET-STATIONS")
                    
                
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
                                    
                                    send_command(producer=producer, operator_id=operator_id,station_id=command[0], type="IRRIGATION", duration=command[1])
                                    
                                else:
                                    
                                    send_command(producer=producer, operator_id=operator_id,station_id=command, type="IRRIGATION", duration=DEFAULT_DURATION)
                                    
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
        
    finally:
        
        stop_event.set()
    
    exit(0)

if __name__ == "__main__":
    main()
