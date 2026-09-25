"""Container smoke example; operator application logic is not implemented yet."""

from kafka import KafkaConsumer, KafkaProducer


def main():
    print(f"Kafka client: {KafkaProducer.__name__}, {KafkaConsumer.__name__}")


if __name__ == "__main__":
    main()
