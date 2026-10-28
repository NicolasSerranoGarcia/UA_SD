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

ARG WS_ID=1
ARG ENGINE_IP=127.0.0.1
ARG ENGINE_HOSTNAME=
ARG ENGINE_PORT=8080
ARG CENTRAL_HOSTNAME=altaria.proxy.rlwy.net
ARG CENTRAL_PORT=40931
ARG LOG_FILE=/var/log/wm-ws/wm-ws-monitor.log
ENV WS_ID=${WS_ID} \
    ENGINE_IP=${ENGINE_IP} \
    ENGINE_HOSTNAME=${ENGINE_HOSTNAME} \
    ENGINE_PORT=${ENGINE_PORT} \
    CENTRAL_HOSTNAME=${CENTRAL_HOSTNAME} \
    CENTRAL_PORT=${CENTRAL_PORT} \
    LOG_FILE=${LOG_FILE}
RUN mkdir -p /var/log/wm-ws

CMD ["sh", "-c", "set -- ./build/WM_WS_M --ws-id \"$WS_ID\" --engine-port \"$ENGINE_PORT\" --central-hostname \"$CENTRAL_HOSTNAME\" --central-port \"$CENTRAL_PORT\" --log-file \"$LOG_FILE\"; if [ -n \"$ENGINE_HOSTNAME\" ]; then set -- \"$@\" --engine-hostname \"$ENGINE_HOSTNAME\"; else set -- \"$@\" --engine-ip \"$ENGINE_IP\"; fi; exec \"$@\""]
