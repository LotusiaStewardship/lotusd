# Build stage
FROM nvidia/cuda:11.8.0-devel-ubuntu22.04 AS builder

# Install only the necessary build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl-dev \
    ocl-icd-opencl-dev \
    && rm -rf /var/lib/apt/lists/*

# Create a working directory
WORKDIR /app

# Copy the binary from the debug folder
COPY target/debug/lotus-miner-cli /app/

# Runtime stage
FROM nvidia/cuda:11.8.0-runtime-ubuntu22.04

# Install only the necessary runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    ocl-icd-libopencl1 \
    clinfo \
    && rm -rf /var/lib/apt/lists/*

# Create symbolic link to the NVIDIA OpenCL library
RUN mkdir -p /etc/OpenCL/vendors && \
    echo "libnvidia-opencl.so.1" > /etc/OpenCL/vendors/nvidia.icd

# Create a working directory
WORKDIR /app

# Copy the kernels folder
COPY kernels /app/kernels

# Copy the binary from the builder stage
COPY --from=builder /app/lotus-miner-cli /app/
# Make the binary executable
RUN chmod +x /app/lotus-miner-cli

# Create the startup script to detect GPUs and launch miners
# This script accepts various parameters as environment variables to customize the miners
# Usage examples:
#
# Basic usage with default settings:
#   docker run --gpus all lotus-miner-cuda
#
# Using a custom miner address:
#   docker run --gpus all lotus-miner-cuda YOUR_LOTUS_ADDRESS
#
# Customizing parameters via environment variables:
#   docker run --gpus all -e KERNEL_SIZE=24 -e INSTANCES_PER_GPU=2 lotus-miner-cuda YOUR_LOTUS_ADDRESS
#
# Available environment variables:
#   - MINER_ADDRESS: Your Lotus address (can also be passed as the first argument)
#   - KERNEL_SIZE: The kernel size parameter (default: 22)
#   - RPC_URL: The mining pool URL (default: https://burnlotus.org)
#   - RPC_USER: RPC username (default: miner)
#   - RPC_PASSWORD: RPC password (default: password)
#   - RPC_POLL_INTERVAL: Block template polling interval (default: 1)
#   - POOL_MINING: Whether to use pool mining (default: true)
#                  Note: The CLI parameter is --poolmining (lowercase 'm'), not --poolMining (capital 'M')
#   - INSTANCES_PER_GPU: Number of miner instances per GPU (default: 4)
#   - CONFIG_FILE: Path to a config file (default: not used)
RUN echo '#!/bin/bash\n\
# Define an array of addresses\n\
ADDRESSES=(\n\
  "lotus_16PSJLP8LK3q14N9xMFDPUsMPxi8yqvdoy1p4woLz"\n\
  "lotus_16PSJKo9nqwWTiNsqdNkU1haXyxaS2pi15WoNQPX4"\n\
  "lotus_16PSJLkXR2zHXC4JCFmLcY6Tpxb9qLbP9rzcsGSgo"\n\
  "lotus_16PSJNvBpeApatcZVJKAkjSuttJTE3rsYcCCmkoFd"\n\
  "lotus_16PSJHErRNz6nBEsy4rNdm9XcVWRd4EnkUvNMpF28"\n\
  "lotus_16PSJKLDyRfjecZdHipKW3nKiYZNyii5HKWmR25sw"\n\
  "lotus_16PSJNuGwhhcPjHTJ9zukMTVLccaUkAJTEVKB4jFR"\n\
  "lotus_16PSJKLDyRfjecZdHipKW3nKiYZNyii5HKWmR25sw"\n\
  "lotus_16PSJNsKV3q6jpHZfyBeWN36pq161LrMqw2dMbnLZ"\n\
  "lotus_16PSJPtwpyHe4AyvgqEGGaX5HYQQX67KaDKKM4FxF"\n\
  "lotus_16PSJPeRBEy23rYkEJur222qNrFMm6yv1mertXhMu"\n\
  "lotus_16PSJJbXGkMuHJGARrtaazXJ67fC2UEh1YqLZCujF"\n\
  "lotus_16PSJQvP79yJRMqg4gv2raoVVL2uSbwsWBEHvAi8V"\n\
  "lotus_16PSJPCoX9JqXrec5TwuAMtYkvh1fiZ9sTTpfQ31P"\n\
  "lotus_16PSJKoAGBUgtQx8XMRKWMvJee3WMcNS6xt12sGrS"\n\
  "lotus_16PSJMJXAGa66QFhkKhwatUyW962KBhhs3EqEmQbb"\n\
  "lotus_16PSJK7bcJJ9TRhWCvRrM6u8ujQFLSPDUgPWdX1ii"\n\
  "lotus_16PSJLNUo6rjn6SQ3iTM443cRNTWgNqBWaiKwdjjF"\n\
  "lotus_16PSJPexSJkdiaYtF2UUCYCxQXmVtGB1hDnVwDawF"\n\
  "lotus_16PSJMJNsKWckFqsTHUwnAdFr1XJ6HRhN4s7Mdk67"\n\
  "lotus_16PSJLBXesQhGkNfosUXQzwYJZjBjiN6jui799rfu"\n\
  "lotus_16PSJPx5PmS7BiXSGjiG91K2JBxATwLXE15b2FPVx"\n\
  "lotus_16PSJMshen3qpKEVYu5WCTxkU5XAqTYBV2HzRikwa"\n\
  "lotus_16PSJNkBJahGpkhYu4fLt3USVvWxSnD6qYiT7Ssgj"\n\
  "lotus_16PSJNxzcVBciuHukm3pJ5frC19rAjhoPEVYNYJz7"\n\
  "lotus_16PSJNaTGZ2aA7dyCPugNzmSodvE1W2dauNJPJjR9"\n\
  "lotus_16PSJQm6taghN5LUd8kKT9StUcJKDkBGCU6BnCyM3"\n\
)\n\
\n\
# Get the number of available GPUs\n\
GPU_COUNT=$(nvidia-smi --query-gpu=name --format=csv,noheader | wc -l)\n\
echo "Detected $GPU_COUNT GPUs"\n\
\n\
# Set default values for parameters\n\
KERNEL_SIZE=${KERNEL_SIZE:-22}\n\
RPC_URL=${RPC_URL:-"https://burnlotus.org"}\n\
RPC_USER=${RPC_USER:-"miner"}\n\
RPC_PASSWORD=${RPC_PASSWORD:-"password"}\n\
RPC_POLL_INTERVAL=${RPC_POLL_INTERVAL:-1}\n\
POOL_MINING=${POOL_MINING:-true}\n\
INSTANCES_PER_GPU=${INSTANCES_PER_GPU:-4}\n\
CONFIG_PARAM=""\n\
\n\
# Set pool mining flag if enabled\n\
POOL_MINING_FLAG=""\n\
if [[ "$POOL_MINING" == "true" ]]; then\n\
  POOL_MINING_FLAG="--poolmining"\n\
fi\n\
\n\
# Set config file parameter if provided\n\
if [[ -n "$CONFIG_FILE" ]]; then\n\
  CONFIG_PARAM="--config $CONFIG_FILE"\n\
fi\n\
\n\
# Check if a miner address was provided as a parameter or environment variable\n\
MINER_ADDRESS=${MINER_ADDRESS:-""}\n\
if [[ -n "$1" ]]; then\n\
  MINER_ADDRESS="$1"\n\
fi\n\
\n\
if [[ -n "$MINER_ADDRESS" ]]; then\n\
  echo "Using provided miner address: $MINER_ADDRESS"\n\
fi\n\
\n\
# Function to get a random address\n\
get_random_address() {\n\
  # If a miner address was provided, use it\n\
  if [[ -n "$MINER_ADDRESS" ]]; then\n\
    echo "$MINER_ADDRESS"\n\
    return\n\
  fi\n\
  # Otherwise, get random index from integrated addresses\n\
  local idx=$((RANDOM % ${#ADDRESSES[@]}))\n\
  echo ${ADDRESSES[$idx]}\n\
}\n\
\n\
# Display configuration settings\n\
echo "Miner configuration:"\n\
echo "  Kernel size: $KERNEL_SIZE"\n\
echo "  RPC URL: $RPC_URL"\n\
echo "  RPC user: $RPC_USER"\n\
echo "  RPC poll interval: $RPC_POLL_INTERVAL"\n\
echo "  Pool mining: ${POOL_MINING}"\n\
echo "  Instances per GPU: $INSTANCES_PER_GPU"\n\
if [[ -n "$CONFIG_FILE" ]]; then\n\
  echo "  Config file: $CONFIG_FILE"\n\
fi\n\
\n\
# Launch miner instances for each GPU\n\
for ((i=0; i<$GPU_COUNT; i++))\n\
do\n\
  for ((j=0; j<$INSTANCES_PER_GPU; j++))\n\
  do\n\
    ADDRESS=$(get_random_address)\n\
    echo "Starting miner instance $((j+1)) on GPU $i with address $ADDRESS"\n\
    ./lotus-miner-cli \\\n\
      -g $i \\\n\
      -s $KERNEL_SIZE \\\n\
      -o $ADDRESS \\\n\
      -i $RPC_POLL_INTERVAL \\\n\
      -a $RPC_URL \\\n\
      -u $RPC_USER \\\n\
      -p $RPC_PASSWORD \\\n\
      $POOL_MINING_FLAG \\\n\
      $CONFIG_PARAM &\n\
  done\n\
done\n\
\n\
# Keep the container running\n\
wait\n\
' > /app/start-miners.sh && chmod +x /app/start-miners.sh

# Command to run when container starts
# Usage:
# 1. Basic usage with default settings:
#    docker run --gpus all lotus-miner-cuda
#
# 2. Using a custom miner address:
#    docker run --gpus all lotus-miner-cuda YOUR_LOTUS_ADDRESS
#
# 3. Customizing parameters via environment variables:
#    docker run --gpus all -e KERNEL_SIZE=24 -e INSTANCES_PER_GPU=2 lotus-miner-cuda YOUR_LOTUS_ADDRESS
ENTRYPOINT ["/app/start-miners.sh"]