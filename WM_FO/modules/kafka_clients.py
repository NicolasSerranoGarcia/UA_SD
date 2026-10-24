import json
import queue
import threading
from confluent_kafka import Consumer, Producer, KafkaException, KafkaError

FORMAT = "utf-8"

cent_notif_queue = queue.Queue() # Queue for the broadcast messages
response_queue = queue.Queue() # Queue for the unicast response messages
telemetry_queue = queue.Queue() # Queue for the telemetry

##########################
### PRODUCER FUNCTIONS ###
##########################

def send_command(producer: Producer, operator_id: int, station_id: int | None, type: str, duration: int | None = None):
    
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


##########################
### CONSUMER FUNCTIONS ###
##########################


def start_consumers(bootstrap_servers: str, operator_id: int, stop_event: threading.Event):
    """Function that starts two consumers in different threads, each one listening to a different kafka topic"""
    
    # Thread for 'central-notis' topic
    thread_notifications = threading.Thread(
        target=_generic_kafka_consumer,
        args=(bootstrap_servers, operator_id, "central-notis", stop_event),
        daemon=True
    )
    
    # Thread for 'telemetry' topic
    thread_telemetry = threading.Thread(
        target=_generic_kafka_consumer,
        args=(bootstrap_servers, operator_id, "telemetry", stop_event),
        daemon=True
    )
    
    thread_notifications.start()
    thread_telemetry.start()    

def _generic_kafka_consumer(bootstrap_servers: str, operator_id: int, topic: str, stop_event: threading.Event):
    """Function that creates a generic consumer"""
    
    config = {
        "bootstrap.servers": bootstrap_servers,
        "group.id": f"group-notis-{operator_id}",
        "auto.offset.reset": "latest",
    }
    try:
        
        consumer = Consumer(config)
        consumer.subscribe([topic])
        
    except Exception as e:
        
        print("[KAFKA ERROR] Failed creating consumer")
    
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
            
            data: dict = json.loads(msg.value().decode(FORMAT))
            
            if topic == "central-notis":
                
                target_fo = data.get("operator_id")
                
                if target_fo == operator_id: # Unicast message
                    
                    response_queue.put(data)
                    
                elif target_fo is None: # Broadcast message
                    
                    cent_notif_queue.put(data)
                    
                else:
                    
                    continue
                
            elif topic == "telemetry":
                
                telemetry_queue.put(data)
        
        except Exception as e:
            
            print(f"[ERROR PARSING MESSAGE] Consumer of topic {topic}: {e}")
        
    consumer.close()
