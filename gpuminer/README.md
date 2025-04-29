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
