FROM ubuntu:22.04

# Install dependencies if needed
RUN apt-get update && apt-get install -y \
    libssl-dev \
    ocl-icd-opencl-dev \
    ocl-icd-libopencl1 \
    && rm -rf /var/lib/apt/lists/*

# Create a working directory
WORKDIR /app

# Copy the kernels folder
COPY kernels /app/kernels

# Copy the binary from the debug folder
COPY target/debug/lotus-miner-cli /app/
# Make the binary executable
RUN chmod +x /app/lotus-miner-cli

# Command to run when container starts
ENTRYPOINT ["./lotus-miner-cli", "-p", "password", "-i", "1", "-a", "http://10.128.71.115:10609", "-u", "miner", "-o", "lotus_16PSJHzZXfxuDVuDMD2k8bdHL1AN7pnryQSUwLt77"] 