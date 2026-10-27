import time
import os
import subprocess

RESET = "\033[0m"
TEXT_BLACK = "\033[30m"
TEXT_WHITE = "\033[97m"

STATUS_STYLES = {
    "Available": f"\033[42m{TEXT_BLACK}",
    "Watering": f"\033[5;42m{TEXT_BLACK}",
    "Leak": f"\033[41m{TEXT_WHITE}",
    "Out of service": f"\033[48;5;208m{TEXT_BLACK}",
    "Disconnected": f"\033[100m{TEXT_WHITE}",
}

def print_menu():
    """Procedure that prints the menu"""
    
    print("1.Start Irrigation")
    print("2.Block station")
    print("3.Activate station")
    print("4.Show stations")
    print("5.Read requests from file")
    print("6.Exit")

def flush_cmd():
    """Procedure that clears the terminal"""
    
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
    

def read_file(file_path: str, format: str = 'utf-8') -> list:
    """Function that reads a file with commands with format: <station_id>[#<duration>]"""
    
    try:
        
        with open(file_path, mode="r", encoding=format) as file:
            
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
                    commands.append(int(command[0]))
                
                
    except OSError:
        
        print(f"[ERROR] File '{file_path}' does not exists. Try again")
        return None
                  
            
    return commands

def print_station(station: tuple):
    
    station_id, location, status = station
    
    style = STATUS_STYLES.get(status, default="")
    
    status_badge = f"{style} {status:<15} {RESET}" if style else f"{status:<15}"
    
    print(f"{str(station_id):<8} {location:<25} {status_badge}")