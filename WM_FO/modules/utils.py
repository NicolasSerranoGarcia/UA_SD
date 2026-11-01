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
    
def parse_commands_content(content: str) -> list:
    """Parses lines with format: <station_id>[#<duration>] and returns a list of tuples."""
    
    commands = []
    
    for line in content.splitlines():
        
        command = line.strip()
        
        if not command:
            
            continue
        
        parts = command.split("#")
        
        if len(parts) >= 2:
            
            commands.append((int(parts[0].strip()), int(parts[1].strip())))
            
        else:
            
            
            commands.append((int(parts[0].strip()), None))
            
    return commands

def read_file(file_path: str, format: str = 'utf-8') -> list | None:
    
    try:
        
        with open(file_path, mode="r", encoding=format) as file:
            
            return parse_commands_content(file.read())
        
    except OSError:
        
        print(f"[ERROR] File '{file_path}' does not exist.")
        return None
    
    except ValueError:
        
        print(f"[ERROR] Invalid format inside '{file_path}'.")
        return None

def print_station(station: tuple):
    
    station_id, location, status = station
    
    style = STATUS_STYLES.get(status, default="")
    
    status_badge = f"{style} {status:<15} {RESET}" if style else f"{status:<15}"
    
    print(f"{str(station_id):<8} {location:<25} {status_badge}")