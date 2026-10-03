"""Container smoke example; Central application logic is not implemented yet."""

import sqlite3
import sys
import argparse
from confluent_kafka import Producer, Consumer

def main():
    
    argparser = argparse.ArgumentParser(description="Central for Water Management Network")
    
    argparser.add_argument("port", type=int, help="Listening port of the socket server")
    argparser.add_argument("kafka_ip", type=str, help="IP of the kafka bootstrap server")
    argparser.add_argument("kafka_port", type=int, help="Port of the kafka bootstrap server")
    
    args = argparser.parse_args()
    
    port = args.port
    kafka_ip = args.kafka_ip
    kafka_port = args.kafka_port
    
    
    sys.exit(0)
    


if __name__ == "__main__":
    main()

