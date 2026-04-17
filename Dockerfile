FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config python3 \
    libssl-dev libzmq3-dev libboost-all-dev libevent-dev \
    libdb++-dev libminiupnpc-dev libjemalloc-dev \
    libsqlite3-dev flatbuffers-compiler libflatbuffers-dev \
    libprotobuf-dev protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /usr/lib/cmake/flatbuffers && \
    echo 'set(FLATBUFFERS_INCLUDE_DIR "/usr/include")' > /usr/lib/cmake/flatbuffers/FlatbuffersConfig.cmake && \
    echo 'set(FLATBUFFERS_LIBRARIES "/usr/lib/x86_64-linux-gnu/libflatbuffers.a")' >> /usr/lib/cmake/flatbuffers/FlatbuffersConfig.cmake && \
    echo 'set(FLATBUFFERS_FOUND TRUE)' >> /usr/lib/cmake/flatbuffers/FlatbuffersConfig.cmake

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_BITCOIN_QT=OFF \
    -DBUILD_BITCOIN_SEEDER=OFF \
    -DBUILD_BITCOIN_NNG=OFF \
    -DENABLE_UPNP=OFF \
    -DENABLE_BIP70=OFF \
    && ninja -C build -j$(nproc) lotusd lotus-cli \
    && strip build/src/lotusd build/src/lotus-cli

# ── Runtime ─────────────────────────────────────────────────────────────────────
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3t64 libzmq5 libboost-filesystem1.83.0 libboost-thread1.83.0 \
    libevent-2.1-7t64 libevent-pthreads-2.1-7t64 \
    libdb5.3++ libminiupnpc17 libjemalloc2 libsqlite3-0 \
    libprotobuf-lite32t64 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN groupadd -r lotus && useradd -rmg lotus lotus

COPY --from=builder /src/build/src/lotusd     /usr/local/bin/
COPY --from=builder /src/build/src/lotus-cli   /usr/local/bin/
RUN chmod +x /usr/local/bin/lotusd /usr/local/bin/lotus-cli

ENV LOTUS_DATA=/data
RUN mkdir -p "$LOTUS_DATA" && chown lotus:lotus "$LOTUS_DATA"
VOLUME ["$LOTUS_DATA"]

# P2P mainnet / RPC mainnet / P2P testnet / RPC testnet
EXPOSE 10605 10604 11605 11604

USER lotus
ENTRYPOINT ["lotusd"]
CMD ["-datadir=/data", "-printtoconsole", \
     "-listen=1", "-dnsseed=1", \
     "-addnode=seed.lotusia.org"]
