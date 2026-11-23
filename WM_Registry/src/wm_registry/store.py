import hashlib
import hmac
import os
import re
import secrets
import sqlite3
from contextlib import contextmanager
from pathlib import Path


MAX_STATION_ID = (1 << 63) - 1


def database_path():
    return Path(os.getenv("DB_DIR", "./data")) / "stations.db"


def valid_station_id(station_id):
    return 1 <= station_id <= MAX_STATION_ID


def valid_city(city):
    return isinstance(city, str) and re.fullmatch(r"[a-z0-9-]{1,64}", city) is not None


def _digest(secret):
    if not isinstance(secret, str) or not re.fullmatch(r"[A-Za-z0-9_-]{43}", secret):
        return None
    return hashlib.sha256(secret.encode("ascii")).digest()


def _matches(stored, candidate):
    return stored is not None and candidate is not None and hmac.compare_digest(stored, candidate)


@contextmanager
def _connection(path, *, write=False):
    db = sqlite3.connect(path, timeout=2)
    try:
        with db:
            if write:
                db.execute("BEGIN IMMEDIATE")
            yield db
    finally:
        db.close()


def initialize(path):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with _connection(path) as db:
        db.execute("PRAGMA journal_mode=WAL")
        db.execute("""
            CREATE TABLE IF NOT EXISTS registry_enrollments (
                station_id INTEGER PRIMARY KEY CHECK (station_id > 0),
                city TEXT,
                credential_digest BLOB
            )
        """)


def reset_enrollment(path, station_id):
    if not valid_station_id(station_id):
        raise ValueError("Station ID must be a positive SQLite integer.")
    with _connection(path, write=True) as db:
        return db.execute("DELETE FROM registry_enrollments WHERE station_id=?", (station_id,)).rowcount == 1


def enroll(path, station_id, city):
    credential = secrets.token_urlsafe(32)
    with _connection(path, write=True) as db:
        result = db.execute("""
            INSERT INTO registry_enrollments (station_id, city, credential_digest)
            VALUES (?, ?, ?)
            ON CONFLICT(station_id) DO UPDATE SET city=excluded.city, credential_digest=excluded.credential_digest
            WHERE registry_enrollments.credential_digest IS NULL
        """, (station_id, city, _digest(credential)))
        return credential if result.rowcount == 1 else None


def _verified_city(db, station_id, credential):
    candidate = _digest(credential)
    if candidate is None:
        return None
    row = db.execute("SELECT city, credential_digest FROM registry_enrollments WHERE station_id=?",
                     (station_id,)).fetchone()
    return row[0] if row and _matches(row[1], candidate) else None


def verify_station(path, station_id, credential):
    if not valid_station_id(station_id):
        return None
    with _connection(path) as db:
        return _verified_city(db, station_id, credential)


def revoke(path, station_id, credential):
    with _connection(path, write=True) as db:
        if _verified_city(db, station_id, credential) is None:
            return False
        db.execute("DELETE FROM registry_enrollments WHERE station_id=?", (station_id,))
        return True
