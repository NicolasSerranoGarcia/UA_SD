import os
import subprocess
from modules.shared.shared_status import add_log

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
                    commands.append(int(command[0]), None)
                
                
    except OSError:
        
        add_log(f"[ERROR]", f"File '{file_path}' does not exists. Try again")
        return None
            
    except ValueError:
        add_log(f"[ERROR]", f"Invalid format inside '{file_path}'.")
        return None      
            
    return commands

def print_station(station: tuple):
    
    station_id, location, status = station
    
    style = STATUS_STYLES.get(status, default="")
    
    status_badge = f"{style} {status:<15} {RESET}" if style else f"{status:<15}"
    
    print(f"{str(station_id):<8} {location:<25} {status_badge}")