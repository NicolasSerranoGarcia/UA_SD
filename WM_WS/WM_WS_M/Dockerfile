FROM gcc:14

WORKDIR /usr/src/water_management_app/watering_station/monitor

RUN apt-get update && apt-get install -y --no-install-recommends cmake git ca-certificates qt6-base-dev qt6-base-dev-tools libboost-program-options-dev libboost-url-dev\
    && rm -rf /var/lib/apt/lists/*

# Build with WM_WS as the context so the shared HCP header is available.
COPY WM_WS_M/ .
COPY shared/ ../shared/

ARG BUILD_GUI=OFF
ARG BUILD_TESTING=OFF
RUN cmake -B build -S . -DBUILD_TESTING=${BUILD_TESTING} -DBUILD_GUI=${BUILD_GUI} \
    && cmake --build build/

CMD ["./build/WM_WS_M"]
