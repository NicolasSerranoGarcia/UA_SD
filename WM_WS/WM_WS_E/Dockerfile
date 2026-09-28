FROM gcc:14

WORKDIR /usr/src/water_management_app/watering_station/engine

RUN apt-get update && apt-get install -y --no-install-recommends librdkafka-dev cmake pkg-config \
    && rm -rf /var/lib/apt/lists/*

COPY . .

RUN cmake -B build -S . && cmake --build build/

CMD ["./build/WM_WS_E"]
