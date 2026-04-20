#!/bin/bash
# GPU miner launcher used by the lotus-gpu-miner (NVIDIA) and
# lotus-gpu-miner-amd (AMD/ROCm) Docker images.
#
# Detects the available GPUs at runtime and spawns INSTANCES_PER_GPU
# copies of `lotus-miner-cli` per device, optionally rotating through a
# pool of integrated lotus_ addresses when no explicit `-o` / `--mine-to-address`
# was supplied.

set -uo pipefail

ADDRESSES=(
  "lotus_16PSJLP8LK3q14N9xMFDPUsMPxi8yqvdoy1p4woLz"
  "lotus_16PSJKo9nqwWTiNsqdNkU1haXyxaS2pi15WoNQPX4"
  "lotus_16PSJLkXR2zHXC4JCFmLcY6Tpxb9qLbP9rzcsGSgo"
  "lotus_16PSJNvBpeApatcZVJKAkjSuttJTE3rsYcCCmkoFd"
  "lotus_16PSJHErRNz6nBEsy4rNdm9XcVWRd4EnkUvNMpF28"
  "lotus_16PSJKLDyRfjecZdHipKW3nKiYZNyii5HKWmR25sw"
  "lotus_16PSJNuGwhhcPjHTJ9zukMTVLccaUkAJTEVKB4jFR"
  "lotus_16PSJKLDyRfjecZdHipKW3nKiYZNyii5HKWmR25sw"
  "lotus_16PSJNsKV3q6jpHZfyBeWN36pq161LrMqw2dMbnLZ"
  "lotus_16PSJPtwpyHe4AyvgqEGGaX5HYQQX67KaDKKM4FxF"
  "lotus_16PSJPeRBEy23rYkEJur222qNrFMm6yv1mertXhMu"
  "lotus_16PSJJbXGkMuHJGARrtaazXJ67fC2UEh1YqLZCujF"
  "lotus_16PSJQvP79yJRMqg4gv2raoVVL2uSbwsWBEHvAi8V"
  "lotus_16PSJPCoX9JqXrec5TwuAMtYkvh1fiZ9sTTpfQ31P"
  "lotus_16PSJKoAGBUgtQx8XMRKWMvJee3WMcNS6xt12sGrS"
  "lotus_16PSJMJXAGa66QFhkKhwatUyW962KBhhs3EqEmQbb"
  "lotus_16PSJK7bcJJ9TRhWCvRrM6u8ujQFLSPDUgPWdX1ii"
  "lotus_16PSJLNUo6rjn6SQ3iTM443cRNTWgNqBWaiKwdjjF"
  "lotus_16PSJPexSJkdiaYtF2UUCYCxQXmVtGB1hDnVwDawF"
  "lotus_16PSJMJNsKWckFqsTHUwnAdFr1XJ6HRhN4s7Mdk67"
  "lotus_16PSJLBXesQhGkNfosUXQzwYJZjBjiN6jui799rfu"
  "lotus_16PSJPx5PmS7BiXSGjiG91K2JBxATwLXE15b2FPVx"
  "lotus_16PSJMshen3qpKEVYu5WCTxkU5XAqTYBV2HzRikwa"
  "lotus_16PSJNkBJahGpkhYu4fLt3USVvWxSnD6qYiT7Ssgj"
  "lotus_16PSJNxzcVBciuHukm3pJ5frC19rAjhoPEVYNYJz7"
  "lotus_16PSJNaTGZ2aA7dyCPugNzmSodvE1W2dauNJPJjR9"
  "lotus_16PSJQm6taghN5LUd8kKT9StUcJKDkBGCU6BnCyM3"
)

# Detect GPUs. Try NVIDIA first, then AMD ROCm, then fall back to one.
if command -v nvidia-smi &>/dev/null; then
  GPU_COUNT=$(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | wc -l)
elif command -v rocm-smi &>/dev/null; then
  GPU_COUNT=$(rocm-smi --showid 2>/dev/null | grep -cE '^GPU\[' || echo 1)
else
  echo "Warning: neither nvidia-smi nor rocm-smi found. Defaulting GPU_COUNT=1."
  GPU_COUNT=1
fi
[[ "$GPU_COUNT" -lt 1 ]] && GPU_COUNT=1
echo "Detected $GPU_COUNT GPU(s)"

GPU_INDEX=${GPU_INDEX:-0}
KERNEL_SIZE=${KERNEL_SIZE:-22}
RPC_URL=${RPC_URL:-"https://burnlotus.org"}
RPC_USER=${RPC_USER:-"miner"}
RPC_PASSWORD=${RPC_PASSWORD:-"password"}
RPC_POLL_INTERVAL=${RPC_POLL_INTERVAL:-1}
POOL_MINING=${POOL_MINING:-true}
INSTANCES_PER_GPU=${INSTANCES_PER_GPU:-4}
CONFIG_FILE=${CONFIG_FILE:-""}

PASSTHROUGH_ARGS=""
ADDR_FOUND=false
MINER_ADDRESS=""

if [[ $# -eq 1 && "$1" == lotus_* ]]; then
  MINER_ADDRESS="$1"
  ADDR_FOUND=true
else
  while [[ $# -gt 0 ]]; do
    case "$1" in
      -o|--mine-to-address)   MINER_ADDRESS="$2"; ADDR_FOUND=true; shift 2 ;;
      -g|--gpu-index)         GPU_INDEX="$2";                       shift 2 ;;
      -s|--kernel-size)       KERNEL_SIZE="$2";                     shift 2 ;;
      -i|--rpc-poll-interval) RPC_POLL_INTERVAL="$2";               shift 2 ;;
      -a|--rpc-url)           RPC_URL="$2";                         shift 2 ;;
      -u|--rpc-user)          RPC_USER="$2";                        shift 2 ;;
      -p|--rpc-password)      RPC_PASSWORD="$2";                    shift 2 ;;
      -c|--config)            CONFIG_FILE="$2";                     shift 2 ;;
      -m|--poolmining)        POOL_MINING=true;                     shift   ;;
      -d|--debug)             PASSTHROUGH_ARGS="$PASSTHROUGH_ARGS $1"; shift ;;
      *)                      PASSTHROUGH_ARGS="$PASSTHROUGH_ARGS $1"; shift ;;
    esac
  done
fi

POOL_MINING_FLAG=""
[[ "$POOL_MINING" == "true" ]] && POOL_MINING_FLAG="--poolmining"

CONFIG_PARAM=""
[[ -n "$CONFIG_FILE" ]] && CONFIG_PARAM="--config $CONFIG_FILE"

[[ -n "$MINER_ADDRESS" ]] && echo "Using provided miner address: $MINER_ADDRESS"

get_random_address() {
  if [[ -n "$MINER_ADDRESS" ]]; then
    echo "$MINER_ADDRESS"
    return
  fi
  local idx=$((RANDOM % ${#ADDRESSES[@]}))
  echo "${ADDRESSES[$idx]}"
}

echo "Miner configuration:"
echo "  Kernel size:       $KERNEL_SIZE"
echo "  RPC URL:           $RPC_URL"
echo "  RPC user:          $RPC_USER"
echo "  RPC poll interval: $RPC_POLL_INTERVAL"
echo "  Pool mining:       $POOL_MINING"
echo "  Instances per GPU: $INSTANCES_PER_GPU"
[[ -n "$CONFIG_FILE" ]] && echo "  Config file:       $CONFIG_FILE"

for ((i=0; i<GPU_COUNT; i++)); do
  for ((j=0; j<INSTANCES_PER_GPU; j++)); do
    ADDRESS=$(get_random_address)
    echo "Starting miner instance $((j+1)) on GPU $i with address $ADDRESS"
    /opt/lotus/bin/lotus-miner-cli \
      -g "$i" \
      -s "$KERNEL_SIZE" \
      -o "$ADDRESS" \
      -i "$RPC_POLL_INTERVAL" \
      -a "$RPC_URL" \
      -u "$RPC_USER" \
      -p "$RPC_PASSWORD" \
      $POOL_MINING_FLAG \
      $CONFIG_PARAM \
      $PASSTHROUGH_ARGS &
  done
done

wait
