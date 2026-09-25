FROM gcc:14

WORKDIR /usr/src/water_management_app/watering_station/engine

RUN apt-get update && apt-get install -y --no-install-recommends librdkafka-dev \
    && rm -rf /var/lib/apt/lists/*

COPY . .

RUN g++ -std=c++23 -Wall -Wextra -pthread main.cpp -o WM_WS_E -lrdkafka

CMD ["./WM_WS_E"]
