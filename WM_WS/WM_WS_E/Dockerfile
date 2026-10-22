FROM gcc:14

WORKDIR /usr/src/water_management_app/watering_station/engine

RUN apt-get update && apt-get install -y --no-install-recommends librdkafka-dev cmake git ca-certificates pkg-config libboost-program-options-dev libboost-url-dev\
    && rm -rf /var/lib/apt/lists/*

# Build with WM_WS as the context so the shared HCP header is available.
COPY WM_WS_E/ .
COPY shared/ ../shared/

ARG BUILD_TESTING=OFF
RUN cmake -B build -S . -DBUILD_TESTING=${BUILD_TESTING} \
    && cmake --build build/

CMD ["./build/WM_WS_E"]
