#include <iostream>
#include <librdkafka/rdkafka.h>

int main() {
    std::cout << "Kafka client: " << rd_kafka_version_str() << "\n";
    return 0;
}
