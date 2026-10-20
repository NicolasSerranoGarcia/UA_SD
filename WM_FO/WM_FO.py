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

def read_file(file_path: str):

    return
    
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
                    
                    file_path = input("Enter the path of the file: ")
                    read_file(file_path)
                    
                case _:
                    print("Invalid option")
            
            time.sleep(2)
            flush_cmd()
            
            print_menu()
            input_option = int(input("Select an option: "))
    
    except KeyboardInterrupt:
        print("\nInterrupted.Exiting...")
        exit(0)
    

if __name__ == "__main__":
    main()
