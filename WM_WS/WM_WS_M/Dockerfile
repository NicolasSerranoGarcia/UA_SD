FROM gcc:14

WORKDIR /usr/src/water_management_app/watering_station/monitor

RUN apt-get update && apt-get install -y --no-install-recommends cmake libboost-program-options-dev libboost-url-dev\
    && rm -rf /var/lib/apt/lists/*

COPY . .

RUN cmake -B build -S . && cmake --build build/

CMD ["./build/WM_WS_M"]
