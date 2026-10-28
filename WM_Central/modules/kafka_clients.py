import json
import queue
import threading
from confluent_kafka import Consumer, Producer, KafkaException, KafkaError, TopicPartition
from confluent_kafka.admin import AdminClient

FORMAT = "utf-8"

_producer: Producer | None = None
_stop_event: threading.Event | None = None

def start_kafka(bootstrap_servers: str, stop_event: threading.Event, topic_handlers: dict):
    
    global _producer, _stop_event
    
    _stop_event = stop_event
    
    try:
        
        kafka_config = {
            "bootstrap.servers": bootstrap_servers,
            "socket.timeout.ms": 2000,
            "message.timeout.ms": 3000,
        }
        
        _producer = Producer(kafka_config)
        
    except KafkaException as e:
        
        raise Exception(f"Singleton producer could not be initialized: {e}")

    # Launches a consumer thread for each topic, with its respective handling function
    for topic, handler_func in topic_handlers.items():
        
        consumer_thread = threading.Thread(
            target=_generic_consumer,
            args=(bootstrap_servers, topic, stop_event, handler_func),
            daemon=True
        )
        consumer_thread.start()


def _generic_consumer(bootstrap_servers: str, topic: str, stop_event: threading.Event, handleFunc):

    try:
        
        consumer_config = {
            "bootstrap.servers": bootstrap_servers,
            "group.id": f"group-{topic}",
            "auto.offset.reset": "earliest",
            "enable.auto.commit": True,
            "session.timeout.ms": 6000,
        }
        
        consumer = Consumer(consumer_config)
        consumer.subscribe([topic])
        
        while not stop_event.is_set():
            
            msg = consumer.poll(timeout=1.0)
            
            if msg is None or msg.value() is None: # There is no message available in the topic at the moment
                
                continue
            
            if msg.error():
                
                if msg.error().code() != KafkaError._PARTITION_EOF: # This error just means that the consumer has reached the end of the partition
                    
                    print(f"[KAFKA ERROR] Consumer of topic {topic}: {msg.error()}") # If it is another error, it is printed
                    
                continue
            
            try:
                
                data = json.loads(msg.value().decode(FORMAT))
                handleFunc(data)
            
            except json.JSONDecodeError:
                
                print(f"[KAFKA ERROR] Invalid JSON received from topic {topic}")
            
            except Exception as e:
                
                print(f"[KAFKA ERROR] Error procesing message in topic {topic}: {e}")
    
    except Exception as e:
        
        print(f"[KAFKA ERROR] Error in consumer for topic '{topic}': {e}")
    
    finally:
        
        consumer.close()

def send_event(topic: str, msg: dict, key: str | None = None):
    """Funtion that sends a message to a specific topic, with a possible key"""
    
    if _producer is None:
        
        print("[KAFKA ERROR] Producer was not initialized. Command ignored")
        return
    
    try:
        
        msg_bytes = json.dumps(msg).encode(FORMAT)
        key_bytes = key.encode(FORMAT) if key else None
        
        _producer.produce(topic=topic, value=msg_bytes, key=key_bytes)
        _producer.poll(0)
        
    except Exception as e:
        
        print(f"[KAFKA ERROR] Producer unable to send message to {topic}: {e}")
    
def stop_kafka():
    """Function that clears all the pending messages before exiting the program"""
    
    if _producer:
        
        _producer.flush(timeout=3)

    if _stop_event:
        
        _stop_event.set()

