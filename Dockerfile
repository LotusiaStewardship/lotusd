# Build stage
FROM ubuntu:24.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libssl-dev \
    libzmq3-dev \
    libboost-all-dev \
    libevent-dev \
    libdb++-dev \
    libminiupnpc-dev \
    libnng-dev \
    curl \
    wget \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set working directory for the source code
WORKDIR /lotus-source

# Copy the source code (you need to have your source in the same directory as Dockerfile)
COPY . .

# Build the binary
RUN mkdir build && \
    cd build && \
    cmake .. && \
    make lotusd -j32

# Runtime stage
FROM ubuntu:24.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libgomp1 \
    libssl3 \
    libzmq5 \
    libboost-system1.83.0 \
    libboost-filesystem1.83.0 \
    libboost-thread1.83.0 \
    libboost-program-options1.83.0 \
    libevent-2.1-7 \
    libevent-pthreads-2.1-7 \
    libdb5.3++ \
    libatomic1 \
    curl \
    libminiupnpc17 \
    libnng-dev \
    wget \
    && rm -rf /var/lib/apt/lists/*

# Create directory structure
RUN mkdir -p /opt/lotus/bin /opt/lotus/lib /opt/lotus/include

# Copy built binary from builder stage
COPY --from=builder /lotus-source/build/src/lotusd /opt/lotus/bin/

# Set permissions
RUN chmod +x /opt/lotus/bin/*

# Add to PATH and library path
ENV PATH="/opt/lotus/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/lotus/lib:/usr/local/lib:/usr/lib/x86_64-linux-gnu"

# Create data directory
RUN mkdir -p /root/.lotus

# Make sure the library is in the system's library path
RUN echo "/usr/lib/x86_64-linux-gnu" > /etc/ld.so.conf.d/lotus.conf && ldconfig

# Expose commonly used ports
# P2P network
EXPOSE 9333
# RPC interface 
EXPOSE 9332
# RPC port from docker-compose
EXPOSE 58525

# Set working directory
WORKDIR /root

# Default command to run the node daemon
ENTRYPOINT ["/opt/lotus/bin/lotusd"]
CMD ["-reindex", "-printtoconsole", "-datadir=/root/.lotus", "-addnode=116.103.232.62", "-addnode=69.234.67.246", "-addnode=146.70.211.96", "-addnode=70.92.25.116", "-addnode=147.135.88.232:10605", "-addnode=147.135.88.233:10605", "-addnode=45.119.84.253:10605", "-addnode=35.184.152.63:10605", "-addnode=147.135.88.232:10605", "-addnode=147.135.88.233:10605", "-addnode=45.119.84.253:10605", "-addnode=35.184.152.63:10605", "-rpcuser=lotus", "-rpcpassword=lkdjflheoiueiiir", "-rpcallowip=10.128.0.0/16", "-rpcbind=0.0.0.0:58525", "-rpcport=58525", "-rpcallowip=127.0.0.1", "-rpcthreads=8", "-disablewallet=0"]