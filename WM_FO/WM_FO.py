import argparse
import time
import os
import subprocess
from confluent_kafka import Consumer, Producer

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
    
    
def main():

    argparser = argparse.ArgumentParser(description="Field Operator for Water Management Network")
    
    argparser.add_argument("ID", type=int, help="Unique ID of the field operator")
    argparser.add_argument("--kafka-ip", type=str, default="iriguchi.proxy.rlwy.net", help="IP of the kafka bootstrap server")
    argparser.add_argument("--kafka-port", type=int, default=37040, help="Port of the kafka bootstrap server")
    
    args = argparser.parse_args()

    operator_id = args.ID
    kafka_ip = args.kafka_ip
    kafka_port = args.kafka_port
    
      
    print_menu()
    try:    
        input_option = int(input("Select an option: "))
        
        while input_option != 6:
            
            match input_option:
                
                case 1:
                    print("Starting irrigation...")
                    
                case 2:
                    print("Blocking station...")
                    
                case 3  :
                    print("Activating station...")
                    
                case 4:
                    print("Showing stations...")
                    
                case 5:
                    
                    while True:
                        
                        file_path = input("Enter the path of the file ('q' to cancel): ").strip()
                        
                        if file_path == 'q':
                            
                            break
                        
                        comms = read_file(file_path=file_path)
                        
                        if comms is not None:
                            
                            # TODO
                            print(comms)
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
