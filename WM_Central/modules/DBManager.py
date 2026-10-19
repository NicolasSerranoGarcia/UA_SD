import os
import sqlite3
import threading
import queue
import time

DB_DIR = os.getenv("DB_DIR","./data")
DB_PATH = os.path.join(DB_DIR, "stations.db") 

_db_queue = queue.Queue()
_writer_started = False


def get_db_connection():
    
    os.makedirs(DB_DIR, exist_ok=True) 
    return sqlite3.connect(DB_PATH)

def _db_writer_worker():
    
    conn = get_db_connection()
    conn.execute("PRAGMA journal_mode=WAL;") # This is done to improve the concurrency in SQLite
    try:
        
        while True:
            
            request = _db_queue.get()
            
            if request is None:
                _db_queue.task_done()
                break
            
            op, data = request
            try:
                
                cursor = conn.cursor()
                if op == "INSERT_STATION":
                    
                    station_id, station_location, station_status = data
                    cursor.execute( """
                                    INSERT INTO stations (id, location, status) VALUES (?, ?, ?)
                                    """, (station_id, station_location, station_status))    
                    
                elif op == "INSERT_OPERATOR":
                    
                    operator_id = data
                    cursor.execute( """
                                    INSERT INTO operators (id) VALUES (?)
                                    """, (operator_id,))    
                    
                elif op == "SET_STATION_STATUS":
                    
                    station_id, status = data
                    cursor.execute( """
                                    UPDATE stations SET status = ? WHERE id = ?
                                    """, (status, station_id))           
                
                conn.commit()
            
            except Exception as e:
                
                print("DB ERROR: ", e)
                
            finally:
                
                _db_queue.task_done()
    
    except Exception as e:
    
        print("DB ERROR: ", e)
    
    finally:
        
        conn.close()


def init_database():
    
    global _writer_started
    
    
    conn = get_db_connection()
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       CREATE TABLE IF NOT EXISTS stations (
                           
                           id INTEGER PRIMARY KEY,
                           location TEXT NOT NULL,
                           status TEXT CHECK (status IN ('Available', 'Watering', 'Leak', 'Out of service', 'Disconnected'))
                       )
                       """)
        cursor.execute("""
                       CREATE TABLE IF NOT EXISTS operators (
                           
                           id INTEGER PRIMARY KEY
                       )
                       """)
        
        conn.commit()
        
    except Exception as e:
        
        print("DB ERROR: ", e)
        
    finally:
        
        conn.close()
    
    if not _writer_started: # If the writer is not initializated, we initialize it
        
        writer_thread = threading.Thread(target=_db_writer_worker, daemon=True)  
        writer_thread.start()
        _writer_started = True  
    
def stop_database():
    
    _db_queue.put(None)

def store_station(station_id: int, station_location: str, station_status: str = "Disconnected"):
    
    _db_queue.put(("INSERT_STATION", (station_id, station_location, station_status)))
        
def set_station_status(station_id: int, status: str):
    
    _db_queue.put(("SET_STATION_STATUS", (station_id, status)))
        
        
def store_operator(operator_id: int):
    
    _db_queue.put(("INSERT_OPERATOR", operator_id))
        
def read_stations():
    
    conn = get_db_connection()
    conn.execute("PRAGMA journal_mode=WAL;")
    
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       SELECT * FROM stations
                       """)
        
        return cursor.fetchall()
        
    except Exception as e:
        
        print("DB ERROR: ", e)
        return []
        
    finally:
        
        conn.close()
        
        
def check_station(station_id: int) -> bool:
    
    conn = get_db_connection()
    conn.execute("PRAGMA journal_mode=WAL;")
    
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       SELECT EXISTS(SELECT 1 FROM stations WHERE id = ?)
                       """, (station_id,))
        
        return cursor.fetchone()[0]
        
    except Exception as e:
        
        print("DB ERROR: ", e)
        return False
        
    finally:
        
        conn.close()    
    
    
        
def check_operator(operator_id: int) -> bool:
    
    conn = get_db_connection()
    conn.execute("PRAGMA journal_mode=WAL;")
    
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       SELECT EXISTS(SELECT 1 FROM operators WHERE id = ?)
                       """, (operator_id,))
        
        return cursor.fetchone()[0]
        
    except Exception as e:
        
        print("DB ERROR: ", e)
        return False
        
    finally:
        
        conn.close()
    
def _erase_db():
    """
    THIS IS A PRIVATE FUNCTION THAT SHOULD NOT BE USED UNLESS YOU WANT TO DELETE ALL YOUR DB
    
    """
    conn = get_db_connection()

    try:
        
        cursor = conn.cursor()
        cursor.execute("DELETE FROM stations")
        cursor.execute("DELETE FROM operators")
        conn.commit()
        
    except Exception as e:
        
        return None
    
    finally:
        
        conn.close()


if __name__ == "__main__":
    
    init_database()
    store_station(10, "Alicante")
    store_station(11, "Barcelona", "Leak")
    set_station_status(11, "Available")
    store_operator(13)
    
    _db_queue.join()
    
    print("Stored stations: ", read_stations())
    print(check_operator(13), check_operator(14))
    _erase_db()
    
    
    