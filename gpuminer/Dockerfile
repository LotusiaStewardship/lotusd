FROM nvidia/cuda:11.8.0-devel-ubuntu22.04

# Install dependencies for CUDA and OpenCL support
RUN apt-get update && apt-get install -y \
    libssl-dev \
    ocl-icd-opencl-dev \
    ocl-icd-libopencl1 \
    opencl-headers \
    clinfo \
    && rm -rf /var/lib/apt/lists/*

# Create symbolic link to the NVIDIA OpenCL library
RUN mkdir -p /etc/OpenCL/vendors && \
    echo "libnvidia-opencl.so.1" > /etc/OpenCL/vendors/nvidia.icd

# Create a working directory
WORKDIR /app

# Copy the kernels folder
COPY kernels /app/kernels

# Copy the binary from the debug folder
COPY target/debug/lotus-miner-cli /app/
# Make the binary executable
RUN chmod +x /app/lotus-miner-cli

# Command to run when container starts
ENTRYPOINT ["./lotus-miner-cli", "-g", "0", "-s", "26", "-o", "lotus_16PSJHmYLT6dWHD4uYKazP58uhHnKsSPTonLB8s9y", "-i", "1", "-a", "https://burnlotus.org", "--poolmining"]