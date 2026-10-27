import json
import queue
import threading
from confluent_kafka import Consumer, Producer, KafkaException, KafkaError, TopicPartition
from confluent_kafka.admin import AdminClient

FORMAT = "utf-8"

central_notis_queue = queue.Queue() # Queue for the unicast response messages
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

def _num_partitions_for_topic(bootstrap_servers: str, topic: str):
    """Function that consults the number of partitions of a specific topic"""
    
    admin_client = AdminClient({"bootstrap.servers": bootstrap_servers})

    metadata = admin_client.list_topics(topic=topic, timeout=5.0)

    if topic in metadata.topics:
        return len(metadata.topics[topic].partitions)
    else:
        raise Exception(f"Topic {topic} does not exist in the cluster")


def _generic_kafka_consumer(bootstrap_servers: str, operator_id: int, topic: str, stop_event: threading.Event):
    """Function that creates a generic consumer"""
    
    config = {
        "bootstrap.servers": bootstrap_servers,
        "group.id": f"group-notis-{operator_id}",
        "auto.offset.reset": "latest",
    }
    
    try:
        
        consumer = Consumer(config)
        
        num_particiones = _num_partitions_for_topic(bootstrap_servers, topic)
        dest_partition = operator_id % num_particiones
        tp = TopicPartition(topic=topic, partition=dest_partition)
        
        consumer.assign([tp])
        
    except Exception as e:
        
        print(f"[KAFKA ERROR] Failed creating consumer: {e}")
    
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
                
                central_notis_queue.put(data)
                
            elif topic == "telemetry":
                
                telemetry_queue.put(data)
        
        except Exception as e:
            
            print(f"[ERROR PARSING MESSAGE] Consumer of topic {topic}: {e}")
        
    consumer.close()
