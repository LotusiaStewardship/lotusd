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

**Note**: The `--gpus all` flag requires the NVIDIA Container Toolkit to be installed if you're using NVIDIA GPUs. For AMD GPUs, you may need a different configuration.

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

#### 🔴 AMD GPU Setup

1. Install AMD ROCm drivers:
   ```bash
   sudo apt update
   sudo apt install -y rocm-opencl
   ```

2. Install Docker:
   ```bash
   sudo apt update
   sudo apt install -y docker.io
   sudo systemctl enable --now docker
   sudo usermod -aG docker $USER  # Log out and back in after this
   ```

3. Install OpenCL libraries:
   ```bash
   sudo apt update
   sudo apt install -y ocl-icd-libopencl1 ocl-icd-opencl-dev clinfo
   ```

4. Verify your AMD GPU is detected:
   ```bash
   clinfo  # Should list your AMD GPU as an OpenCL platform
   ```

5. Run with AMD GPU:
   ```bash
   docker run --device=/dev/kfd --device=/dev/dri \
     -v /usr/lib/x86_64-linux-gnu/libOpenCL.so.1:/usr/lib/x86_64-linux-gnu/libOpenCL.so.1 \
     -v /etc/OpenCL:/etc/OpenCL \
     ghcr.io/boblepointu/lotus-gpu-miner:latest \
     --rpc-password password --rpc-poll-interval 1 --rpc-url https://burnlotus.org --rpc-user miner \
     --mine-to-address lotus_16PSJPZTD2aXDZJSkCYfdSC4jzkVzHk1JQGojw2BN --kernel-size 21 --poolmining
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
3. Build `lotus-miner` using `cargo build`
4. Run the lotus miner with `./target/debug/lotus-miner.exe --rpc-user=<user> --rpc-password=<password> --mine-to-address=<your lotus address>`

### 🍎 MacOS

1. Install [rustup](https://rustup.rs/)
2. Install the rust toolchain using rustup
3. Build `lotus-miner` using `cargo build`
4. Run the lotus miner with `./target/debug/lotus-miner --rpc-user=<user> --rpc-password=<password> --mine-to-address=<your lotus address>`

---

<p align="center">
  <strong>🌸 Happy Mining! 🌸</strong>
</p>

