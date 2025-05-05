<p align="center">
  <h1>🌸 Lotus GPU Miner 🌸</h1>
</p>

<p align="center">
  <a href="https://github.com/Boblepointu/lotusd/actions/workflows/lotus-main-ci.yml">
    <img src="https://github.com/Boblepointu/lotusd/actions/workflows/lotus-main-ci.yml/badge.svg?branch=master" alt="CI Status">
  </a>
  <a href="https://github.com/LotusiaStewardship/lotusd/releases/latest">
    <img src="https://img.shields.io/github/v/release/LotusiaStewardship/lotusd" alt="Latest Release">
  </a>
  <a href="../LICENSE">
    <img src="https://img.shields.io/github/license/LotusiaStewardship/lotusd" alt="License">
  </a>
  <a href="https://opencl.org">
    <img src="https://img.shields.io/badge/OpenCL-Compatible-brightgreen" alt="OpenCL Compatible">
  </a>
</p>

<div align="center">
  
  > *"Harness the power of your GPU for optimal Lotus mining"*
  
  <hr>
</div>

The Lotus GPU miner enables high-performance mining for the Lotus network, using OpenCL to harness the power of your GPU for optimal mining efficiency.

## ⚙️ Configuration

Configuration may be specified on the command line or via a toml file. 
The configuration file location by default is: `~/.lotus-miner/config.toml`

The configuration file looks like the following:

```
mine_to_address = "lotus_16PSJMStv9sve3DfhDpiwUCa7RtqkyNBoS8RjFZSt"
rpc_url = "http://127.0.0.1:10605"
rpc_poll_interval = 3
rpc_user = "lotus"
rpc_password = "lotus"
gpu_index = 0
kernel_size = 23
pool_mining = false
```

See `lotus-miner --help` for a description of the parameters.

## 🏊‍♂️ Mining Modes

### 🌊 Pool Mining

You can enable pool mining mode with the `--poolmining` flag. When this flag is set, the miner will submit blocks using a format compatible with mining pools.

```bash
./lotus-miner --poolmining
```

You can also enable pool mining in the config file:

```
pool_mining = true
```

## 🚀 Quick Start Examples

### 📦 Using Pre-built Binary

You can download the latest release of the Lotus GPU Miner from the [Releases page](https://github.com/LotusiaStewardship/lotusd/releases).

#### 🏊‍♂️ Example: Mining on a Pool

```bash
# Replace with your own mining pool details and address
# Note: For pool mining, username and password can be any dummy values
lotus-miner-cli --rpc-password password --rpc-poll-interval 1 --rpc-url https://burnlotus.org --rpc-user miner --mine-to-address lotus_16PSJPZTD2aXDZJSkCYfdSC4jzkVzHk1JQGojw2BN --kernel-size 21 --poolmining
```

#### 🔒 Example: Solo Mining

```bash
lotus-miner-cli --rpc-password your_password --rpc-poll-interval 3 --rpc-url http://127.0.0.1:10604 --rpc-user your_username --mine-to-address your_lotus_address --kernel-size 21
```

### 🐳 Using Docker

You can also run the Lotus GPU Miner using Docker:

```bash
# Pull the Docker image
docker pull ghcr.io/boblepointu/lotus-gpu-miner:latest

# Run the container with pool mining
# Note: For pool mining, username and password can be any dummy values
docker run --gpus all -it --rm \
  -v /usr/lib/x86_64-linux-gnu/libOpenCL.so.1:/usr/lib/x86_64-linux-gnu/libOpenCL.so.1 \
  -v /etc/OpenCL:/etc/OpenCL \
  ghcr.io/boblepointu/lotus-gpu-miner:latest \
  --rpc-password password --rpc-poll-interval 1 --rpc-url https://burnlotus.org --rpc-user miner \
  --mine-to-address lotus_16PSJPZTD2aXDZJSkCYfdSC4jzkVzHk1JQGojw2BN --kernel-size 21 --poolmining

# Run the container for solo mining
docker run --gpus all -it --rm ghcr.io/boblepointu/lotus-gpu-miner:latest \
  --rpc-password your_password --rpc-poll-interval 3 --rpc-url http://your_node_ip:10604 --rpc-user your_username \
  --mine-to-address your_lotus_address --kernel-size 21
```

### 🐋 Using Our Docker Container with Multi-GPU Support

We provide a Docker container with automatic multi-GPU detection that can run multiple miner instances per GPU:

```bash
# Build the Docker container
docker build -t lotus-miner-cuda ./gpuminer

# Run with default random addresses
docker run --gpus all lotus-miner-cuda

# Run with your own miner address
docker run --gpus all lotus-miner-cuda YOUR_LOTUS_ADDRESS

# Example with a specific address
docker run --gpus all lotus-miner-cuda lotus_16PSJHmYLT6dWHD4uYKazP58uhHnKsSPTonLB8s9y
```

#### 🎛️ Customizing Docker Container Parameters

You can customize all aspects of the miner by passing environment variables:

```bash
# Customize kernel size and number of instances per GPU
docker run --gpus all \
  -e KERNEL_SIZE=24 \
  -e INSTANCES_PER_GPU=2 \
  lotus-miner-cuda YOUR_LOTUS_ADDRESS

# Full customization example
docker run --gpus all \
  -e KERNEL_SIZE=23 \
  -e RPC_URL="https://burnlotus.org" \
  -e RPC_USER="miner" \
  -e RPC_PASSWORD="password" \
  -e RPC_POLL_INTERVAL=3 \
  -e POOL_MINING=true \
  -e INSTANCES_PER_GPU=4 \
  lotus-miner-cuda YOUR_LOTUS_ADDRESS
```

#### 📋 Available Environment Variables

The container supports all parameters available to the `lotus-miner-cli` tool:

| Environment Variable | CLI Equivalent        | Description                              | Default Value        |
|----------------------|-----------------------|------------------------------------------|----------------------|
| `MINER_ADDRESS`      | `-o, --mine-to-address` | Your Lotus address                      | (Random from list)   |
| `KERNEL_SIZE`        | `-s, --kernel-size`    | Kernel size parameter                    | 22                   |
| `RPC_URL`            | `-a, --rpc-url`        | Mining pool URL                          | https://burnlotus.org |
| `RPC_USER`           | `-u, --rpc-user`       | RPC username                             | miner                |
| `RPC_PASSWORD`       | `-p, --rpc-password`   | RPC password                             | password             |
| `RPC_POLL_INTERVAL`  | `-i, --rpc-poll-interval` | Block template polling interval       | 1                    |
| `POOL_MINING`        | `-m, --poolmining`     | Whether to use pool mining mode (note: CLI uses lowercase 'm')  | true                 |
| `INSTANCES_PER_GPU`  | N/A                    | Number of miner instances per GPU        | 4                    |
| `CONFIG_FILE`        | `-c, --config`         | Path to a config file                    | (Not used)           |

The container will:
- Automatically detect all available GPUs
- Launch multiple miner instances for each GPU (configurable)
- Use your provided address if specified, or randomly select from a built-in list
- Mine to the official Lotus pool at https://burnlotus.org by default

**Note**: The `--gpus all` flag requires the NVIDIA Container Toolkit to be installed if you're using NVIDIA GPUs. For AMD GPUs, you may need a different configuration.

### 🔍 Command Line Parameters

For reference, here are all parameters supported by the `lotus-miner-cli` tool:

```
USAGE:
    lotus-miner-cli [FLAGS] [OPTIONS]

FLAGS:
    -h, --help          Prints help information
    -m, --poolmining    Enable pool mining mode (note: lowercase 'm' in poolmining, not poolMining)
    -V, --version       Prints version information

OPTIONS:
    -c, --config <config>                          Configuration file
    -g, --gpu-index <gpu_index>                    GPU index
    -s, --kernel-size <kernel_size>                Kernel size
    -o, --mine-to-address <mine_to_address>        Coinbase Output Address
    -p, --rpc-password <rpc_password>              Lotus RPC password
    -i, --rpc-poll-interval <rpc_poll_interval>    Lotus RPC getblocktemplate poll interval
    -a, --rpc-url <rpc_url>                        Lotus RPC address
    -u, --rpc-user <rpc_user>                      Lotus RPC username
```

**Important Note**: The pool mining parameter must be specified as `--poolmining` (lowercase) or `-m`. Using `--poolMining` (with capital 'M') will not be recognized.

### 🛠️ Docker Setup on Ubuntu 24.04

Follow these steps to set up all dependencies needed for running the Lotus GPU Miner in Docker on Ubuntu 24.04:

#### 🟢 NVIDIA GPU Setup

1. Install the NVIDIA driver if not already installed:
   ```bash
   sudo apt update
   sudo apt install -y nvidia-driver-535  # Use the latest version available
   sudo reboot  # Reboot to load the driver
   ```

2. Install the NVIDIA Container Toolkit:
   ```bash
   # Add the NVIDIA Container Toolkit repository
   curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | sudo gpg --dearmor -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg
   curl -s -L https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list | \
     sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' | \
     sudo tee /etc/apt/sources.list.d/nvidia-container-toolkit.list
   sudo sed -i -e '/experimental/ s/^#//g' /etc/apt/sources.list.d/nvidia-container-toolkit.list
   
   # Install the toolkit
   sudo apt-get update
   sudo apt-get install -y nvidia-container-toolkit
   sudo nvidia-ctk runtime configure --runtime=docker
   sudo systemctl restart docker
   ```

3. Install OpenCL libraries:
   ```bash
   sudo apt update
   sudo apt install -y ocl-icd-libopencl1 ocl-icd-opencl-dev clinfo
   ```

4. Verify your NVIDIA GPU is detected:
   ```bash
   nvidia-smi  # Should show your GPU information
   clinfo      # Should list your GPU as an OpenCL platform
   ```

5. Test Docker access to the GPU:
   ```bash
   # Test NVIDIA Container Toolkit
   sudo docker run --rm --gpus all nvidia/cuda:12.0.0-base-ubuntu22.04 nvidia-smi
   
   # Test OpenCL in Docker with volume mounts
   sudo docker run --rm --gpus all \
     -v /usr/lib/x86_64-linux-gnu/libOpenCL.so.1:/usr/lib/x86_64-linux-gnu/libOpenCL.so.1 \
     -v /etc/OpenCL:/etc/OpenCL \
     ghcr.io/boblepointu/lotus-gpu-miner:latest clinfo
   ```

#### 🔍 Troubleshooting

If you encounter `Platform::list: Error retrieving platform list: ApiWrapper(GetPlatformIdsPlatformListUnavailable(10))`, it means the Docker container cannot access OpenCL. Check that:

1. Your OpenCL libraries are correctly installed on the host:
   ```bash
   ls -la /usr/lib/x86_64-linux-gnu/libOpenCL.so.1
   ls -la /etc/OpenCL
   ```

2. You've correctly mounted these libraries into the container:
   ```bash
   docker run --rm -it --gpus all \
     -v /usr/lib/x86_64-linux-gnu/libOpenCL.so.1:/usr/lib/x86_64-linux-gnu/libOpenCL.so.1 \
     -v /etc/OpenCL:/etc/OpenCL \
     ubuntu:22.04 ls -la /usr/lib/x86_64-linux-gnu/libOpenCL.so.1
   ```

3. For NVIDIA, make sure the NVIDIA OpenCL ICD is properly installed:
   ```bash
   sudo apt install -y nvidia-opencl-icd
   ```

## 🛠️ Build & Run

### 🪟 Windows

Assuming you are running the lotus daemon with server mode:

1. Install OpenCL for your GPU. [AMD](https://github.com/GPUOpen-LibrariesAndSDKs/OCL-SDK/releases/download/1.0/OCL_SDK_Light_AMD.exe) or [NVidia](https://developer.nvidia.com/cuda-downloads)
2. Install [rust](https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-msvc/rustup-init.exe)
3. Build `lotus-miner-cli` using `cargo build`
4. Run the lotus miner with:
   ```bash
   ./target/debug/lotus-miner-cli --rpc-user=<user> --rpc-password=<password> --mine-to-address=<your lotus address>
   ```

### 🍎 MacOS

1. Install [rustup](https://rustup.rs/)
2. Install the rust toolchain using rustup
3. Build `lotus-miner-cli` using `cargo build`
4. Run the lotus miner with:
   ```bash
   ./target/debug/lotus-miner-cli --rpc-user=<user> --rpc-password=<password> --mine-to-address=<your lotus address>
   ```

---

<p align="center">
  <strong>🌸 Happy Mining! 🌸</strong>
</p>