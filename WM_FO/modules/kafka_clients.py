import json
import threading
import time
from confluent_kafka import Consumer, Producer, KafkaException, KafkaError, TopicPartition
from confluent_kafka.admin import AdminClient
from modules.shared.shared_status import fo_state, state_lock, central_notis_queue, add_log

FORMAT = "utf-8"

##########################
### PRODUCER FUNCTIONS ###
##########################

def send_command(producer: Producer,
                operator_id: int, 
                station_id: int | None, 
                command_type: str, 
                operation_code: int, 
                duration: int | None = None):
    
    if producer is None:
        add_log("ERROR", "Producer not created. Cannot connect to WM_Central.")
        return
    
    msg = {"operator_id": operator_id, 
           "station_id": station_id, 
           "command_type": command_type,
           "operation_code": operation_code,
           "duration": duration
        }
    
    try: 
        producer.produce(topic="fo-req", value=json.dumps(msg).encode(FORMAT))
        producer.flush(timeout=3.0)
        return True
        
    except Exception as e:
        
        add_log("[ERROR]", f"Failed to send message to Kafka. Impossible to connect with WM_Central: {e}")
        return False


##########################
### CONSUMER FUNCTIONS ###
##########################


def start_consumers(bootstrap_servers: str, operator_id: int, stop_event: threading.Event):
    """Function that starts two consumers in different threads, each one listening to a different kafka topic"""
    
    with state_lock:
        fo_state["operator_id"] = operator_id
    
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

def _num_partitions_for_topic(bootstrap_servers: str, topic: str) -> int:
    """Queries the Kafka cluster for the number of partitions of a specific topic."""
    
    admin_client = AdminClient({"bootstrap.servers": bootstrap_servers})
    try:
        metadata = admin_client.list_topics(topic=topic, timeout=5.0)
    except Exception as e:
        add_log("ERROR", f"[KAFKA ERROR] Error listing topics: {e}")
        return 1

    if topic in metadata.topics:
        return len(metadata.topics[topic].partitions)
    else:
        add_log("ERROR", f"Topic {topic} does not exist in the cluster. Assuming 1 partition.")
        return 1


def _generic_kafka_consumer(bootstrap_servers: str, operator_id: int, topic: str, stop_event: threading.Event):
    """Function that creates a generic consumer"""
    
    config = {
        "bootstrap.servers": bootstrap_servers,
        "group.id": f"group-notis-{operator_id}",
        "auto.offset.reset": "latest",
        "enable.auto.commit": True,
        "session.timeout.ms": 6000,
    }
    
    try:
        
        consumer = Consumer(config)
        
        num_particiones = _num_partitions_for_topic(bootstrap_servers, topic)
        dest_partition = operator_id % num_particiones
        tp = TopicPartition(topic=topic, partition=dest_partition)
        
        consumer.assign([tp])
        add_log("[INFO]", f"Consumer assigned to '{topic}' [Partition: {dest_partition}]")
        
    except (Exception, KafkaException) as e:
        
        add_log("[ERROR]", f"Failed creating consumer for '{topic}': {e}")
        return
    
    try:
        
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
                
                if data.get("operator_id") != operator_id:
                    continue
                
                
                if topic == "central-notis":
                    
                    msg_type = data.get("type")
                    
                    match msg_type:
                        
                        case "RESPONSE":
                            
                            central_notis_queue.put(data)
                            
                        case "SUMMARY":
                            
                            _handle_summary(data.get("summary"))
                            
                        case "ALERT":
                            
                            add_log("ALERT", f"[{data.get('reason')}] {data.get('message', '')}")
                            
                        case "INFO":
                            
                            add_log("INFO", data.get("message", ""))
                            
                        case _:
                            
                            add_log("ERROR", "Unexpected error in central-notis type")
                            
                elif topic == "telemetry":
                    
                    msg_st_id = data.get("station_id")
                    
                    with state_lock:
                        
                        served = fo_state["station_served"]
                        
                        if served is not None and msg_st_id == served:
                            
                            flux = data.get("flux_l_min", 0.0)
                            fo_state["current_flux"] = flux
                            
                            if fo_state["irrigating"]:
                                fo_state["accumulated_volume"] += (flux / 60.0)
                                
                            fo_state["last_telemetry_ts"] = time.strftime("%H:%M:%S")
            
            except Exception as e:
                
                add_log("ERROR", f"[ERROR PARSING MESSAGE] Consumer '{topic}': {e}")
       
    finally:     
        
        consumer.close()
        
def _handle_summary(summary: tuple):
    
    if not summary or len(summary) < 3:
        
        return
    
    origin_station, total_time, total_liters = summary

    with state_lock:
        
        served = fo_state["station_served"]
        
        if served is not None and int(origin_station) == int(served):
            
            avg_flux = round(total_liters / total_time, 2) if (total_time is not None and total_time > 0) else 0
            fo_state["last_summary"] = {
                "station_id": origin_station,
                "total_time": total_time,
                "total_liters": total_liters,
                "avg_flux": avg_flux,
                "timestamp": time.strftime("%H:%M:%S")
            }
            fo_state["irrigating"] = False
            fo_state["accumulated_volume"] = 0.0

    if served is not None and int(origin_station) == int(served):
        
        add_log("SUCCESS", f"Irrigation finished in WS-{origin_station} | {total_time} min | {total_liters} L")