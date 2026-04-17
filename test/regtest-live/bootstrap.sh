#!/usr/bin/env bash
set -euo pipefail

RPC_URL="http://lotusd-node1:12604"
RPC_USER="test"
RPC_PASS="test"
ACTIVATION_HEIGHT=200

rpc_call() {
    local method="$1"
    shift
    local params="${1:-[]}"
    curl -s --user "${RPC_USER}:${RPC_PASS}" \
         --data-binary "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"${method}\",\"params\":${params}}" \
         -H "Content-Type: application/json" \
         "${RPC_URL}" 2>/dev/null
}

rpc_result() {
    local resp
    resp=$(rpc_call "$@")
    local err
    err=$(echo "$resp" | python3 -c "import sys,json; r=json.load(sys.stdin); print(r['error']['message'] if r.get('error') else '')" 2>/dev/null || true)
    if [ -n "$err" ]; then
        echo "[bootstrap] RPC error ($1): $err" >&2
        return 1
    fi
    echo "$resp" | python3 -c "import sys,json; print(json.load(sys.stdin)['result'])" 2>/dev/null
}

echo "[bootstrap] Waiting for node1 RPC to become available..."
until rpc_call "getblockchaininfo" | grep -q '"blocks"'; do
    sleep 0.5
done
echo "[bootstrap] Node1 RPC is up."

echo "[bootstrap] Waiting for node2 to peer with node1..."
while true; do
    conns=$(rpc_call "getnetworkinfo" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['connections'])" 2>/dev/null || echo "0")
    if [ "$conns" -ge 1 ] 2>/dev/null; then
        echo "[bootstrap] Node1 has $conns peer(s)."
        break
    fi
    sleep 0.5
done

echo "[bootstrap] Creating wallet on node1..."
rpc_call "createwallet" "[\"miner\"]" > /dev/null 2>&1 || true

echo "[bootstrap] Getting a fresh address..."
ADDRESS=$(rpc_result "getnewaddress")
echo "[bootstrap] Using address: $ADDRESS"

current=$(rpc_call "getblockchaininfo" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['blocks'])")
echo "[bootstrap] Current height: $current, target: $ACTIVATION_HEIGHT"

if [ "$current" -lt "$ACTIVATION_HEIGHT" ]; then
    needed=$((ACTIVATION_HEIGHT - current))
    echo "[bootstrap] Generating $needed blocks to reach activation height..."
    resp=$(rpc_call "generatetoaddress" "[${needed}, \"${ADDRESS}\"]")
    err=$(echo "$resp" | python3 -c "import sys,json; r=json.load(sys.stdin); print(r['error']['message'] if r.get('error') else '')" 2>/dev/null || true)
    if [ -n "$err" ]; then
        echo "[bootstrap] ERROR generating blocks: $err" >&2
        exit 1
    fi
    new_height=$(rpc_call "getblockchaininfo" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['blocks'])")
    echo "[bootstrap] Done. Chain at height $new_height."
else
    echo "[bootstrap] Already at or past activation height."
fi

echo "[bootstrap] Bootstrap complete. Miners can start."
