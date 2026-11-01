import json
import queue
import threading
import modules.shared.shared_state as st
from confluent_kafka import Consumer, Producer, KafkaException, KafkaError, TopicPartition
from confluent_kafka.admin import AdminClient

FORMAT = "utf-8"

_producer: Producer | None = None
_stop_event: threading.Event | None = None    
_central_notis_partitions = 0

def start_kafka(bootstrap_servers: str, stop_event: threading.Event, topic_handlers: dict):
    
    global _producer, _stop_event, _central_notis_partitions
    
    _stop_event = stop_event
    
    try:
        
        kafka_config = {
            "bootstrap.servers": bootstrap_servers,
            "socket.timeout.ms": 2000,
            "message.timeout.ms": 15000,
        }
        
        _producer = Producer(kafka_config)
        
        # Get the number of partitions of central-notis
        admin = AdminClient({"bootstrap.servers": bootstrap_servers})
        metadata = admin.list_topics(topic="central-notis", timeout=5.0)
        topic_metadata = metadata.topics.get("central-notis")

        if topic_metadata is None or not topic_metadata.partitions:
            raise RuntimeError("Kafka topic 'central-notis' does not exist or has no partitions")

        _central_notis_partitions = len(topic_metadata.partitions)
        
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

    consumer: Consumer = None

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
            
            if _producer: _producer.poll(0)
            
            if msg is None: # First we check if the message is none
                
                continue
            
            if msg.error(): # Then if there has been an error
                
                if msg.error().code() != KafkaError._PARTITION_EOF: # This error just means that the consumer has reached the end of the partition
                    
                    print(f"[KAFKA ERROR] Consumer of topic {topic}: {msg.error()}") # If it is another error, it is printed
                    
                continue
            
            if msg.value() is None: # And finally, if the value of the message if none
                
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
        
        if consumer is not None:
            
            consumer.close()

def send_event(topic: str, msg: dict, key: str | int | None = None):
    """Funtion that sends a message to a specific topic, with a possible key"""
    
    if _producer is None:
        
        print("[KAFKA ERROR] Producer was not initialized. Command ignored")
        return
    
    try:
        
        msg_bytes = json.dumps(msg).encode(FORMAT)
        key_bytes = str(key).encode(FORMAT) if key is not None else None
        
        if topic == "central-notis":
            
            if key is None:
                raise ValueError("Messages to 'central-notis' must include an operator ID key")

            operator_id = int(key)
            if operator_id < 0:
                
                raise ValueError("Operator ID cannot be negative")

            partition = operator_id % _central_notis_partitions
            _producer.produce(
                topic=topic,
                partition=partition,
                value=msg_bytes,
                key=key_bytes,
            )
            
        else:
                
            _producer.produce(topic=topic, value=msg_bytes, key=key_bytes)
        
        
        _producer.poll(0)
        
    except Exception as e:
        
        st.log(f"[KAFKA ERROR] Producer unable to send message to {topic}: {e}")
    
def stop_kafka():
    """Function that clears all the pending messages before exiting the program"""
    
    if _stop_event:
        
        _stop_event.set()
    
    if _producer:
        
        _producer.flush(timeout=3)



