FROM gcc:14

WORKDIR /usr/src/water_management_app/watering_station/monitor

COPY . .

RUN g++ -std=c++23 -Wall -Wextra -pthread main.cpp -o WM_WS_M

CMD ["./WM_WS_M"]
