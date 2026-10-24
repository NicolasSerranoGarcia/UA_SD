import json
import threading
from confluent_kafka import Consumer, Producer, KafkaException, KafkaError

FORMAT = "utf-8"

def send_command(producer: Producer, operator_id: int, station_id: int | None, type: str, duration: int):
    
    if producer is None:
        print("[ERROR] Producer is not created. Command omitted.")
        return
    
    msg = {"operator_id": operator_id, 
           "station_id": station_id, 
           "command_type": type,
           "duration": duration}
    try: 
        producer.produce(topic="fo-req", value=json.dumps(msg).encode(FORMAT))
        producer.flush()
        
    except Exception as e:
        
        print(f"[ERROR] Failed to send message to Kafka: {e}")

def start_consumers(bootstrap_servers: str, operator_id: int, stop_event: threading.Event):
    
    # Thread for 'central-notis' topic
    thread_notifications = threading.Thread(
        target=generic_kafka_consumer,
        args=(bootstrap_servers, f"group-notis-{operator_id}", "central-notis", stop_event),
        daemon=True
    )
    
    # Thread for 'telemetry' topic
    thread_telemetry = threading.Thread(
        target=generic_kafka_consumer,
        args=(bootstrap_servers, f"group-notis-{operator_id}", "telemetry", stop_event),
        daemon=True
    )
    
    thread_notifications.start()
    thread_telemetry.start()    

def generic_kafka_consumer(bootstrap_servers: str, group_id: str, topic: str, stop_event: threading.Event):
    
    config = {
        "bootstrap.servers": bootstrap_servers,
        "group.id": group_id,
        "auto.offset.reset": "latest",
    }
    
    consumer = Consumer(config)
    
    consumer.subscribe([topic])
    
    # While FO has not exited the main program
    while not stop_event.is_set():
        
        msg = consumer.poll(timeout=1.0)
        
        if msg is None: # There are no new messages in the kafka topic to process
            continue
        
        if msg.error(): # If there is an error, the consumer does not process the message
            
            if msg.error().code() != KafkaError._PARTITION_EOF: # This error just means that the consumer has reached the end of the partition
                
                print(f"[KAFKA ERROR] Consumer of topic {topic}: {msg.error()}") # If it is another error, it is printed
                
            continue
        
        try:
            
            data = json.loads(msg.value().decode(FORMAT))
            
            if topic == "central-notis":
                
                process_notification_message(data)
                
            elif topic == "telemetry":
                
                process_telemetry_message(data)
        
        except Exception as e:
            
            print(f"[ERROR PARSING MESSAGE] Consumer of topic {topic}: {e}")
        
    consumer.close()
    
def process_notification_message(data: dict):
    
    pass

def process_telemetry_message(data: dict):
    
    pass