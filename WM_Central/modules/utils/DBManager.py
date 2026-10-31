import os
import sqlite3
import threading
import queue
import time
import argparse

DB_DIR = os.getenv("DB_DIR","./data")
DB_PATH = os.path.join(DB_DIR, "stations.db") 

_db_queue = queue.Queue()
_writer_started = False


def _get_db_connection():
    
    os.makedirs(DB_DIR, exist_ok=True) 
    return sqlite3.connect(DB_PATH)

def _db_writer_worker():
    
    conn = _get_db_connection()
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
                    
                    station_id, station_location, station_status, previous_status, operator_serving = data
                    cursor.execute( """
                                    INSERT INTO stations (id, location, status, previous_status, operator_serving) VALUES (?, ?, ?, ?, ?)
                                    """, (station_id, station_location, station_status, previous_status, operator_serving))    
                    
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
                
                elif op == "SET_STATION_PREVIOUS_STATUS":
                    
                    station_id, previous_status = data
                    cursor.execute( """
                                    UPDATE stations SET previous_status = ? WHERE id = ?
                                    """, (previous_status, station_id))
                
                elif op == "SET_STATION_OPERATOR":
                    
                    station_id, operator_id = data
                    cursor.execute("UPDATE stations SET operator_serving = ? WHERE id = ?", (operator_id, station_id))
                
                conn.commit()
            
            except Exception as e:
                
                print("[DB ERROR]: ", e)
                
            finally:
                
                _db_queue.task_done()
    
    except Exception as e:
    
        print("[DB ERROR]: ", e)
    
    finally:
        
        conn.close()

def init_database():
    
    global _writer_started
    
    
    conn = _get_db_connection()
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
        
        # To not wipe the railway volume, this code just adds the new column "previous_status" if is not created
        try:
            
            cursor.execute("ALTER TABLE stations ADD COLUMN previous_status TEXT CHECK (previous_status IN ('Available', 'Watering', 'Leak', 'Out of service', 'Disconnected'));")
            
        except sqlite3.OperationalError:
            
            pass
        
        # To not wipe the railway volume, this code just adds the new column "operator_serving" if is not created
        try:
            
            cursor.execute("ALTER TABLE stations ADD COLUMN operator_serving INTEGER;")
            
        except sqlite3.OperationalError:
            
            pass
        
        conn.commit()
        
    except Exception as e:
        
        print("[DB ERROR]: ", e)
        
    finally:
        
        conn.close()
    
    if not _writer_started: # If the writer is not initializated, we initialize it
        
        writer_thread = threading.Thread(target=_db_writer_worker, daemon=True)  
        writer_thread.start()
        _writer_started = True  
 
def reset_stations():
    
    conn = _get_db_connection() 
    conn.execute("PRAGMA journal_mode=WAL;")
    
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       UPDATE stations SET status = 'Disconnected', previous_status = NULL, operator_serving = NULL
                       """)
   
        conn.commit()
        return cursor.rowcount
    
    except Exception as e:
        
        print("[DB ERROR]: ", e)
        return 0
    
    finally:
        
        conn.close()
        
        
   
def stop_database():
    
    _db_queue.put(None)
    _db_queue.join()

def store_station(station_id: int,
                station_location: str, 
                station_status: str = "Disconnected", 
                previous_status: str | None = None,
                operator_serving: int | None = None):
    
    _db_queue.put(("INSERT_STATION", (station_id, station_location, station_status, previous_status, operator_serving)))
        
def set_station_status(station_id: int, status: str):
    
    _db_queue.put(("SET_STATION_STATUS", (station_id, status)))
     
def set_station_previous_status(station_id: int, previous_status: str):
    
    _db_queue.put(("SET_STATION_PREVIOUS_STATUS", (station_id, previous_status)))

def set_station_serving_operator(station_id: int, operator: int):
    
    _db_queue.put(("SET_STATION_OPERATOR", (station_id, operator)))
       
def store_operator(operator_id: int):
    
    _db_queue.put(("INSERT_OPERATOR", operator_id))

def read_station(station_id: int):
    
    conn = _get_db_connection()
    conn.execute("PRAGMA journal_mode=WAL;")
        
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       SELECT * FROM stations WHERE id = ?
                       """, (station_id,))
        
        return cursor.fetchall()[0]
        
    except Exception as e:
        
        print("[DB ERROR]: ", e)
        return []
        
    finally:
        
        conn.close()

def get_station_status(station_id: int):
    
    conn = _get_db_connection()
    conn.execute("PRAGMA journal_mode=WAL;")
    
    try:
        
        row = conn.execute("SELECT status FROM stations WHERE id = ?", (station_id,)).fetchone()
        return row[0] if row else None
    
    except Exception as e:
        
        print("[DB ERROR]: ", e)
        return None
    
    finally:
        
        conn.close()

def get_station_previous_status(station_id: int):
    
    conn = _get_db_connection()
    conn.execute("PRAGMA journal_mode=WAL;")
    
    try:
        
        row = conn.execute("SELECT previous_status FROM stations WHERE id = ?", (station_id,)).fetchone()
        return row[0] if row else None
    
    except Exception as e:
        
        print("[DB ERROR]: ", e)
        return None
    
    finally:
        
        conn.close()

def get_station_serving_operator(station_id: int):
    """Function that returns the operator that is serving a certain station"""
    
    conn = _get_db_connection()
    conn.execute("PRAGMA journal_mode=WAL;")
    
    try:
        
        row = conn.execute("SELECT operator_serving FROM stations WHERE id = ?", (station_id,)).fetchone()
        return row[0] if row else None
    
    except Exception as e:
        
        print("[DB ERROR]: ", e)
        return None
    
    finally:
        
        conn.close()

def get_operator_station_served(operator_id):
    """Function that returns the station a certain operator is serving"""
    
    conn = _get_db_connection()
    conn.execute("PRAGMA journal_mode=WAL;")
    
    try:
        
        row = conn.execute("SELECT id FROM stations WHERE operator_serving = ?", (operator_id,)).fetchone()
        return row[0] if row else None
    
    except Exception as e:
        
        print("[DB ERROR]: ", e)
        return None
    
    finally:
        
        conn.close()

def read_all_stations():
    
    conn = _get_db_connection()
    conn.execute("PRAGMA journal_mode=WAL;")
    
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       SELECT id, location, status FROM stations
                       """)
        
        return cursor.fetchall()
        
    except Exception as e:
        
        print("[DB ERROR]: ", e)
        return []
        
    finally:
        
        conn.close()

def read_all_stations_detailed():
    
    conn = _get_db_connection()
    conn.execute("PRAGMA journal_mode=WAL;")
    
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       SELECT id, location, status, operator_serving FROM stations
                       """)
        
        return cursor.fetchall()
    
    except Exception as e:
        
        print("[DB ERROR]: ", e)
        return []
    
    finally:
        
        conn.close()
             
def check_station(station_id: int) -> bool:
    
    conn = _get_db_connection()
    conn.execute("PRAGMA journal_mode=WAL;")
    
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       SELECT EXISTS(SELECT 1 FROM stations WHERE id = ?)
                       """, (station_id,))
        
        return cursor.fetchone()[0]
        
    except Exception as e:
        
        print("[DB ERROR]: ", e)
        return False
        
    finally:
        
        conn.close()    
        
def check_operator(operator_id: int) -> bool:
    
    conn = _get_db_connection()
    conn.execute("PRAGMA journal_mode=WAL;")
    
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       SELECT EXISTS(SELECT 1 FROM operators WHERE id = ?)
                       """, (operator_id,))
        
        return cursor.fetchone()[0]
        
    except Exception as e:
        
        print("[DB ERROR]: ", e)
        return False
        
    finally:
        
        conn.close()
    
def _erase_db():
    """
    THIS IS A PRIVATE FUNCTION THAT SHOULD NOT BE USED UNLESS YOU WANT TO DELETE ALL YOUR DB
    
    """
    conn = _get_db_connection()

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
    
    argparser = argparse.ArgumentParser(description="Parser for DB Manager")
    argparser.add_argument("-d", "--delete", action="store_true", help="If activated, it deletes de DB")
    
    args = argparser.parse_args()
    
    init_database()
    
    if args.delete:
        _erase_db()
        exit()
    
    store_operator(13)
    
    _db_queue.join()
    
    print("Station with id 10: ", read_station(10)) 
    print("Stored stations: ", read_all_stations())
    print(check_operator(13), check_operator(14))
    
    
    TEST_STATIONS = [
    (4,  "River Park",         "Available"),
    (7,  "River Park",         "Watering"),
    (2,  "Riverside Gardens",  "Available"),
    (11, "Riverside Gardens",  "Leak"),
    (9,  "Riverside Gardens",  "Disconnected"),
    (13, "Central Park",       "Watering"),
    (5,  "Central Park",       "Leak"),
    (8,  "Central Park",       "Out of service"),
    (15, "North Roundabout",   "Available"),
    (16, "North Roundabout",   "Disconnected"),
    (20, "Parque de la Ereta", "Available"),
    (21, "Parque de la Ereta", "Disconnected"),
    (22, "Playa del Postiguet","Out of service"),
    (23, "Parque El Palmeral", "Available"),
    (24, "Parque Lo Morant",   "Disconnected"),
    ]
    
    for i, station in enumerate(TEST_STATIONS):
        
        station_id, location, status = station
        
        store_station(station_id, location, status, operator_serving=i)
    _db_queue.join()          # espera a que el worker escriba todo
    
    
    