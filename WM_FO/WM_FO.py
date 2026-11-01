import argparse
import threading
from confluent_kafka import Consumer, Producer, KafkaException
from modules.kafka_clients import start_consumers
from modules.shared.shared_status import add_log
from modules.web.web_api import app
def main():
    
    argparser = argparse.ArgumentParser(description="Field Operator for Water Management Network")
    
    argparser.add_argument("ID", type=int, help="Unique ID of the field operator")
    argparser.add_argument("--kafka-ip", type=str, default="iriguchi.proxy.rlwy.net", help="IP of the kafka bootstrap server")
    argparser.add_argument("--kafka-port", type=int, default=37040, help="Port of the kafka bootstrap server")
    argparser.add_argument("--web-port", type=int, default=None, help="Port for the FO Web UI (default: 5000 + ID)")
    
    args = argparser.parse_args()

    operator_id = args.ID
    web_port = args.web_port if args.web_port else (8000 + operator_id)

    app.config['OPERATOR_ID'] = operator_id
    
    try:
        
        producer = Producer({
            "bootstrap.servers": f"{args.kafka_ip}:{args.kafka_port}",
            "socket.timeout.ms": 2000,
            "message.timeout.ms": 3000,
        })
        app.config['PRODUCER'] = producer
        
        stop_event = threading.Event()
        start_consumers(f"{args.kafka_ip}:{args.kafka_port}", operator_id, stop_event)
        
    except KafkaException as e:
        add_log("ERROR", f"Kafka init error: {e}")

    try:
        
        add_log("INFO", f"Web UI (FO #{operator_id}) -> http://localhost:{web_port}")
        app.run(host="0.0.0.0", port=web_port, debug=False, use_reloader=False)
        
    finally:
        
        stop_event.set()

if __name__ == "__main__":
    main()
