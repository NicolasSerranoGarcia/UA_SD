"""Container smoke example; Central application logic is not implemented yet."""

import sqlite3
from kafka import KafkaConsumer, KafkaProducer


def main():
    print(f"SQLite: {sqlite3.sqlite_version}")
    print(f"Kafka client: {KafkaProducer.__name__}, {KafkaConsumer.__name__}")


if __name__ == "__main__":
    main()
