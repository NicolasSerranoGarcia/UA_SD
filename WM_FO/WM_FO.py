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
    global next_operation_code; next_operation_code = 1
    global station_served
    
    kafka_config = {
        "bootstrap.servers": f"{args.kafka_ip}:{args.kafka_port}",
        "socket.timeout.ms": 2000,
        "message.timeout.ms": 3000,
    }   
    
    stop_event = threading.Event()
    try:
        
        producer = Producer(kafka_config)
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
    
                    operation_code = next_operation_code
                    next_operation_code += 1
                    
                    send_command(producer=producer, 
                                operator_id=operator_id, 
                                station_id=station_id, 
                                type="IRRIGATION", 
                                operation_code=operation_code, 
                                duration=duration)
                    
                    print("Waiting for the central to respond...")
                    
                    time_out = 5.0
                    start_time = time.time()
                    response_found = False
                    
                    while time.time() - start_time < time_out:
                        
                        try:
                        
                            response: dict = central_notis_queue.get(timeout=0.5)
                            
                            if int(response.get("operation_code")) == operation_code and response.get("type") == "RESPONSE":
                                
                                response_found = True
                                print("Message recieved from central: ", end="")
                                
                                if response.get("status") == "OK":
                                    
                                    station_served = station_id
                                    print(f"[SUCCESS] Station {station_id} started irrigating")
                                    
                                    while True:
                                        
                                        try:
                                            
                                            response: dict = central_notis_queue.get(timeout=0.5)
                                            
                                            if response.get("type") == "SUMMARY":
                                                
                                                summary = tuple(response.get("summary"))
                                                origin_station = summary[0]
                                                
                                                if origin_station == station_served:
                                                    
                                                    total_time = summary[1]
                                                    total_liters = summary[2]
                                                    
                                                    if total_time != 0:
                                                        print("\nIrrigation finished. Summary:")
                                                        print(f"# Duration: {total_time} min")
                                                        print(f"# Liters irrigated: {total_liters} L")
                                                        print(f"# Average flux: {total_liters/total_time} L/min")

                                                    else:
                                                        
                                                        print("[ERROR] Irrigation duration cannot be 0")
                                                    
                                                    break
                                            
                                            telemetry: dict = telemetry_queue.get(timeout=1)
                                            
                                            if telemetry.get("station_id") == station_served:
                                                
                                                flux = telemetry.get("flux_l_min")
                                                print(
                                                    f"\r\033[KStation {station_served} | Flujo: {flux} L/min",
                                                    end="",
                                                    flush=True
                                                )
                                            
                                        except queue.Empty:
                                            
                                            continue 
                                    
                                elif response.get("status") == "KO":
                                    
                                    print(f"[ERROR] Failed in start irrigation in station {station_id}. Reason: {response.get("reason")}")
                                    
                                break
                        
                        except queue.Empty:
                            
                            continue
                        
                    if not response_found:
                        
                        print("[CENTRAL ERROR] Timeout reached: central did not send a respond in time")
                        
                    time.sleep(2)
                    flush_cmd()
                        

                    
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
                    
                    operation_code = next_operation_code
                    next_operation_code += 1
                        
                    send_command(producer=producer, 
                                operator_id=operator_id, 
                                station_id=station_id, 
                                type="BLOCK", 
                                operation_code=operation_code)
                    
                    print("Waiting for the central to respond...")
                    
                    time_out = 5.0
                    start_time = time.time()
                    response_found = False
                    
                    while time.time() - start_time < time_out:
                        
                        try:
                        
                            response: dict = central_notis_queue.get(timeout=0.5)
                            
                            if int(response.get("operation_code"))  == operation_code and response.get("type") == "RESPONSE":
                                
                                response_found = True
                                print("Message recieved from central: ", end="")
                                
                                if response.get("status") == "OK":
                                    
                                    station_served = station_id
                                    print(f"[SUCCESS] Station {station_id} blocked successfully")
                                    
                                elif response.get("status") == "KO":
                                    print(f"[ERROR] Failed blocking station {station_id}. Reason: {response.get("reason")}")
                                    
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
                    
                    operation_code = next_operation_code
                    next_operation_code += 1
                    
                    send_command(producer=producer, 
                                operator_id=operator_id, 
                                station_id=station_id, 
                                type="ACTIVATE", 
                                operation_code=operation_code) 
                     
                    print("Waiting for the central to respond...")
                    
                    time_out = 5.0
                    start_time = time.time()
                    response_found = False
                    
                    while time.time() - start_time < time_out:
                        
                        try:
                        
                            response: dict = central_notis_queue.get(timeout=0.5)
                            
                            if int(response.get("operation_code")) == operation_code and response.get("type") == "RESPONSE":
                                
                                response_found = True
                                print("Message recieved from central: ", end="")
                                
                                if response.get("status") == "OK":
                                    
                                    station_served = station_id
                                    print(f"[SUCCESS] Station {station_id} activated successfully")
                                    
                                elif response.get("status") == "KO":
                                    print(f"[ERROR] Failed activating station {station_id}. Reason: {response.get("reason")}")
                                    
                                break
                        
                        except queue.Empty:
                            
                            continue
                        
                    if not response_found:
                        
                        print("[CENTRAL ERROR] Timeout reached: central did not send a respond in time")
                        
                    time.sleep(2)
                    flush_cmd()

  
                # SHOW ALL THE STATIONS   
                case '4':
                    
                    operation_code = next_operation_code
                    next_operation_code += 1
                    
                    send_command(producer=producer, 
                                operator_id=operator_id, 
                                station_id=None, 
                                type="GET-STATIONS",
                                operation_code=operation_code)
                    
                    print("Waiting for the central to respond...")
                    
                    time_out = 5.0
                    start_time = time.time()
                    response_found = False
                    
                    while time.time() - start_time < time_out:
                        
                        try:
                        
                            response: dict = central_notis_queue.get(timeout=0.5)
                            
                            if int(response.get("operation_code")) == operation_code and response.get("type") == "RESPONSE":
                                
                                response_found = True
                                
                                if response.get("status") == "OK":
                                    
                                    stations = response.get("stations")
                                    
                                    print(f"{'ID':<8} {'Location':<25} {'Status':<15}")
                                    print("="*50)
                                    for station in stations:
                                        
                                        print_station(tuple(station))
                    
                                    
                                elif response.get("status") == "KO":
                                    print(f"[ERROR] Failed getting stations. Reason: {response.get("reason")}")
                                    
                                break
                        
                        except queue.Empty:
                            
                            continue
                        
                    if not response_found:
                        
                        print("[CENTRAL ERROR] Timeout reached: central did not send a respond in time")
                        
                    time.sleep(2)
                    flush_cmd()     
                    
                
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
                                
                                operation_code = next_operation_code
                                next_operation_code += 1
                                
                                if isinstance(command, tuple):
                                    
                                    station_id, duration = command
                                    
                                    send_command(producer=producer, 
                                                operator_id=operator_id, 
                                                station_id=station_id, 
                                                type="IRRIGATION",
                                                operation_code=operation_code, 
                                                duration=duration)
                                    
                                else:
                                    
                                    station_id = command
                                    duration = None
                                    
                                    send_command(producer=producer, 
                                                operator_id=operator_id, 
                                                station_id=station_id, 
                                                type="IRRIGATION", 
                                                operation_code=operation_code,
                                                duration=duration)
                                
                                print(f"Irrigation request sent to station {station_id}" + (f" with duration {duration}" if duration else ""))
                                print("Waiting for the central to respond...")
                                
                                time_out = 5.0
                                start_time = time.time()
                                response_found = False
                                
                                while time.time() - start_time < time_out:
                                    
                                    try:
                                    
                                        response: dict = central_notis_queue.get(timeout=0.5)
                                        
                                        if int(response.get("operation_code")) == operation_code and response.get("type") == "RESPONSE":
                                            
                                            response_found = True
                                            
                                            if response.get("status") == "OK":
                                                
                                                print(f"[SUCCESS] Station {station_id} started irrigating")
                                
                                                
                                            elif response.get("status") == "KO":
                                                
                                                print(f"[ERROR] Failed in start irrigation in station {station_id}. Reason: {response.get("reason")}")
                                                
                                            break
                                    
                                    except queue.Empty:
                                        
                                        continue
                                    
                                if not response_found:
                                    
                                    print("[CENTRAL ERROR] Timeout reached: central did not send a respond in time")
                
                                time.sleep(4)
                            
                            print(f"All commands from file {file_path} sent.")
                            time.sleep(2)
                            flush_cmd()
                            
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
