FROM gcc:14

WORKDIR /opt/wm-w

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake libboost-program-options-dev libboost-url-dev libboost-json-dev \
    && rm -rf /var/lib/apt/lists/*

COPY CMakeLists.txt CmakeOptions.txt ./
COPY cmake/ cmake/
COPY include/ include/
COPY src/ src/
RUN cmake -S . -B build -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel 2

ENV WM_W_CONFIG_FILE=/etc/wm-w/cities.json
ENTRYPOINT ["/opt/wm-w/build/WM_W"]
