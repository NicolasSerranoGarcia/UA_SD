import os
import sqlite3

DB_DIR = os.getenv("DB_DIR","./data")
DB_PATH = os.path.join(DB_DIR, "stations.db") 

def get_db_connection():
    
    os.makedirs(DB_DIR, exist_ok=True) 
    return sqlite3.connect(DB_PATH)


def init_database():
    
    conn = get_db_connection()
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       CREATE TABLE IF NOT EXISTS stations (
                           
                           id INTEGER PRIMARY KEY,
                           location TEXT NOT NULL,
                           status TEXT
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

def store_station(station_id: int, station_location: str, station_status: str = "Disconnected"):
    
    conn = get_db_connection()
    
    try:
        cursor = conn.cursor()
        cursor.execute("""
                       INSERT INTO stations (id, location, status) VALUES (?, ?, ?)
                        """, (station_id, station_location, station_status))
        
        conn.commit()
        
    except Exception as e:
        
        print("DB ERROR: ", e)
        
    finally:
        
        conn.close()
        
def set_station_status(station_id: int, status: str):
    
    conn = get_db_connection()
    
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       UPDATE stations SET status = ? WHERE id = ?
                       """, (status, station_id))
        
        
        conn.commit()
    
    except Exception as e:
        
        print("DB ERROR: ", e)
        
    finally:
        
        conn.close()
    
        
def store_operator(operator_id: int):
    
    conn = get_db_connection()
    
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       INSERT INTO operators (id) VALUES (?)
                       """, (operator_id,))
        
        conn.commit()
        
    except Exception as e:
        
        print("DB ERROR: ", e)
        
    finally:
        
        conn.close()
        
def read_stations():
    
    conn = get_db_connection()
    
    try:
        
        cursor = conn.cursor()
        cursor.execute("""
                       SELECT * FROM stations
                       """)
        
        return cursor.fetchall()
        
    except Exception as e:
        
        print("DB ERROR: ", e)
        
    finally:
        
        conn.close()
        
        
def check_station(station_id: int) -> bool:
    
    conn = get_db_connection()
    
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
    store_station(11, "Barcelona", "Leaked")
    set_station_status(11, "Activated")
    store_operator(13)
    print("Stored stations: ", read_stations())
    print(check_operator(13), check_operator(14))
    _erase_db()
    