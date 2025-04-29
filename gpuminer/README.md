# Lotus GPU Miner

The Lotus GPU miner is a simple non-pool miner for the Lotus network. It
uses OpenCL to mine Lotus blocks on your GPU.

# Configuration

Configuration may be specified on the command line or via a toml file. 
The configuration file location by default is: `~/.lotus-miner/config.toml`

The configuration file looks like the following:

```
mine_to_address = "bchtest:qqegajxrzx9juvg9fuu4cqvndz3u2yz6eg6jfudlvh"
rpc_url = "http://127.0.0.1:10605"
rpc_poll_interval = 3
rpc_user = "lotus"
rpc_password = "lotus"
gpu_index = 0
kernel_size = 23
pool_mining = false
```

See `lotus-miner --help` for a description of the parameters.

## Pool Mining

If you're mining in a pool, you can enable pool mining mode with the `--poolmining` flag. When this flag is set, the miner will submit blocks without the miner address parameter, which is required by some mining pools.

```
./lotus-miner --poolmining
```

You can also enable pool mining in the config file:

```
pool_mining = true
```

# Quick Start Examples

## Using Pre-built Binary

You can download the latest release of the Lotus GPU Miner from the [Releases page](https://github.com/givelotus/lotusd/releases).

### Example: Mining on a Pool

```bash
# Replace with your own mining pool details and address
# Note: For pool mining, username and password can be any dummy values
lotus-miner-cli --rpc-password password --rpc-poll-interval 1 --rpc-url https://pool.golden-flux.fr --rpc-user miner --mine-to-address lotus_16PSJPZTD2aXDZJSkCYfdSC4jzkVzHk1JQGojw2BN --kernel-size 21 --poolmining
```

### Example: Solo Mining

```bash
lotus-miner-cli --rpc-password your_password --rpc-poll-interval 3 --rpc-url http://127.0.0.1:10604 --rpc-user your_username --mine-to-address your_lotus_address --kernel-size 21
```

## Using Docker

You can also run the Lotus GPU Miner using Docker:

```bash
# Pull the Docker image
docker pull givelotus/lotus-gpu-miner:latest

# Run the container with pool mining
# Note: For pool mining, username and password can be any dummy values
docker run --gpus all -it --rm givelotus/lotus-gpu-miner:latest \
  lotus-miner-cli --rpc-password password --rpc-poll-interval 1 --rpc-url https://pool.golden-flux.fr --rpc-user miner \
  --mine-to-address lotus_16PSJPZTD2aXDZJSkCYfdSC4jzkVzHk1JQGojw2BN --kernel-size 21 --poolmining

# Run the container for solo mining
docker run --gpus all -it --rm givelotus/lotus-gpu-miner:latest \
  lotus-miner-cli --rpc-password your_password --rpc-poll-interval 3 --rpc-url http://your_node_ip:10604 --rpc-user your_username \
  --mine-to-address your_lotus_address --kernel-size 21
```

Note: The `--gpus all` flag requires the NVIDIA Container Toolkit to be installed if you're using NVIDIA GPUs. For AMD GPUs, you may need a different configuration.

# Build & Run

## Windows

Assuming you are running the lotus daemon with server mode:

1. Install OpenCL for your GPU. [AMD](https://github.com/GPUOpen-LibrariesAndSDKs/OCL-SDK/releases/download/1.0/OCL_SDK_Light_AMD.exe) or [NVidia](https://developer.nvidia.com/cuda-downloads)
2. Install [rust](https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-msvc/rustup-init.exe)
3. Build `lotus-miner` using `cargo build`
4. Run the lotus miner with `./target/debug/lotus-miner.exe --rpc-user=<user> --rpc-password=<password> --mine-to-address=<your lotus address>.

## MacOS:

1. Install [rustup](https://rustup.rs/)
2. Install the rust toolchain using rustup.
3. Build `lotus-miner` using `cargo build`
4. Run the lotus miner with `./target/debug/lotus-miner --rpc-user=<user> --rpc-password=<password> --mine-to-address=<your lotus address>.