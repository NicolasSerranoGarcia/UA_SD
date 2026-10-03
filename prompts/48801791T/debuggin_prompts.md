### *Is this dockerfile correct for the pass of arguments?*
```docker
FROM python:3

WORKDIR /usr/src/water_management_app/core/central

ENTRYPOINT ["python", "-u", "./WM_Central.py"]

COPY requirements.txt ./

RUN pip install --no-cache-dir -r requirements.txt

COPY . . 
```

### *Could you tell me if my current code is correct, focusing on argument handling?*
```python
import sys
import os
import argparse
import socket
import threading
import sqlite3
from confluent_kafka import Producer, Consumer

def main():
    
    argparser = argparse.ArgumentParser(description="Central for Water Management Network")
    
    argparser.add_argument("port", type=int, default=8080, help="Listening port of the socket server")
    argparser.add_argument("kafka_ip", type=str, default=os.getenv("KAFKA_IP", "iriguchi.proxy.rlwy.net"), help="IP of the kafka bootstrap server")
    argparser.add_argument("kafka_port", type=int, default=os.getenv("KAFKA_PORT", 37040),help="Port of the kafka bootstrap server")
    
    args = argparser.parse_args()
    
    server_ip = socket.gethostbyname(socket.gethostname())
    server_port = args.port
    kafka_ip = args.kafka_ip
    kafka_port = args.kafka_port
    
    validation_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    validation_server.bind((server_ip, server_port))
    validation_server.listen()
    
    sys.exit(0)
    


if __name__ == "__main__":
    main()

```
