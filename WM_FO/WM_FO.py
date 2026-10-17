"""Container smoke example; operator application logic is not implemented yet."""

from confluent_kafka import Consumer, Producer


def main():
    print(f"Kafka client: {Producer.__name__}, {Consumer.__name__}")


if __name__ == "__main__":
    main()
