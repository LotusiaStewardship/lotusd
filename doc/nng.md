# NNG Indexer Interface

This allows external indexers, light wallets, explorer backends, and
mining stack components to be built efficiently using an easy to use NNG
interface. Two transports are exposed:

- A **REQ/REP** RPC socket (`-nngrpc=<url>`) for synchronous queries.
- A **PUB/SUB** event socket (`-nngpub=<url>`) for blockchain notifications.

URLs may use either `tcp://` (network) or `ipc://` (named pipe).

---

## RPC calls

Lotusd v11.0 ships with three categories of RPC calls on the NNG socket.
All requests use the `RpcCall` envelope; all responses use the `RpcResult`
envelope (`is_success`, `error_code`, `error_msg`, `data`).

### 1. Binary block / mempool fetchers (v1, unchanged)

These are the original calls Chronik and similar indexers were built on.
Optimized for high-throughput indexing — payloads are pre-decoded into
typed flatbuffers tables.

| Request                  | Response                  | Purpose |
|--------------------------|---------------------------|---------|
| `GetBlockRequest`        | `GetBlockResponse`        | Fetch one block (by height or hash) |
| `GetBlockRangeRequest`   | `GetBlockRangeResponse`   | Fetch a contiguous range of blocks |
| `GetBlockSliceRequest`   | `GetBlockSliceResponse`   | Slice raw bytes from a `blk?????.dat` file |
| `GetUndoSliceRequest`    | `GetUndoSliceResponse`    | Slice raw bytes from a `rev?????.dat` file |
| `GetMempoolRequest`      | `GetMempoolResponse`      | Snapshot of the current mempool |

### 2. HTTP-mirror typed calls (v2, new in 11.0)

These mirror the most-hit `/api/v1/*` JSON endpoints. Each typed call is
discriminated by an enum on the request union, so clients get stable
codegen and don't have to encode magic path strings. Responses carry the
JSON body the corresponding HTTP handler produced — same shape, same
validation, same error envelopes.

| Request                    | HTTP route(s) it mirrors                                                                       |
|----------------------------|------------------------------------------------------------------------------------------------|
| `GetChainInfoRequest`      | `GET /chain`, `/chain/tip`, `/chain/best-block-hash`, `/chain/block-count` (flag-selectable)   |
| `GetTxRequest`             | `GET /txs?txid=<hex>[&verbose=&blockhash=]`                                                    |
| `GetAddressRequest`        | `GET /addresses?address=<addr>[&limit=&offset=]`                                               |
| `GetUtxosRequest`          | `GET /utxos?address=<addr>[&limit=&offset=]`                                                   |
| `GetMiningInfoRequest`     | `GET /mining`, `/mining/difficulty`, `/mining/networkhashps`, `/mining/estimatefee`            |
| `GetStratumInfoRequest`    | `GET /stratum`, `/stratum/workers`, `/sharechain`, `/scryptchains` (scope enum)                |

Multi-route typed calls (`GetChainInfoRequest`, `GetMiningInfoRequest`,
`GetStratumInfoRequest`) return a `[SubResponse]` so a single round-trip
can fetch everything atomically. Each `SubResponse` carries `path`,
`status_code`, and the raw JSON `body`.

### 3. Generic REST tunnel (v2, new in 11.0)

For everything not on the typed list — including the entire legacy
JSON-RPC allowlist at `/api/v1/rpc/<method>` — use `RestCall`:

```
table RestCall {
  method: string;            // "GET", "POST", "OPTIONS"
  path: string;              // path relative to /api/v1/, e.g. "rpc/getblock"
  query: [KvPair];           // url query params
  body: [ubyte];             // raw request body (JSON for POSTs)
  content_type: string;      // optional
}

table RestCallResponse {
  status_code: uint16;       // HTTP status the handler emitted
  content_type: string;      // Content-Type the handler set
  body: [ubyte];             // raw response bytes
}
```

The same read-only policy the unauthenticated HTTP layer enforces is
applied here:

- `GET` and `OPTIONS` are unconditionally allowed.
- `POST` is restricted to:
  - `txs/decode`, `txs/decoderawtransaction`, `scripts/decode` — pure
    decoders that take a hex blob and return its parsed form.
  - `rpc/<method>` where `<method>` is on `LegacyRpcAllowlist()` — a
    read-only or consensus-validated broadcast (e.g.
    `sendrawtransaction`, `submitblock`, `getrawtransaction`,
    `getblockchaininfo`, …).
- Anything else is rejected with `METHOD_NOT_ALLOWED`.

`RestCall.body` is capped at `-maxapibodysize` bytes (default 1 MiB);
larger bodies are rejected with `BODY_TOO_LARGE`.

### Error codes

| `error_code` | Meaning |
|--------------|---------|
| 0            | success |
| 2            | invalid flatbuffer encoding |
| 3            | unknown RPC method (request union arm not built into this binary) |
| 4            | unknown block-id type |
| 5            | block not found |
| 6            | block data corrupted |
| 7            | invalid block slice |
| 8            | invalid request (required field missing/empty) |
| 9            | method not allowed (verb + path not on the read-only surface) |
| 10           | not found (no `/api/v1` route matches) |
| 11           | handler failed to produce a response |
| 12           | request body exceeds `-maxapibodysize` |

---

## PubSub messages

Enabled with `-nngpub=<url>` and one or more `-nngpubmsg=<msg>`.
Available message types:

- `updateblktip` — chain tip moved (after a chain of updates). Payload: `UpdatedBlockTip`.
- `mempooltxadd` — transaction added to mempool. Payload: `TransactionAddedToMempool`.
- `mempooltxrem` — transaction removed from mempool (expiry, conflict, sizelimit, reorg). Payload: `TransactionRemovedFromMempool`.
- `blkconnected` — block connected to the active chain. Payload: `BlockConnected`.
- `blkdisconctd` — block disconnected (reorg, `invalidateblock`). Payload: `BlockDisconnected`.
- `chainstflush` — block database flushed to disk. Payload: `ChainStateFlushed`.

Messages have their type prepended as the first 12 bytes (zero-padded if
shorter), followed by the corresponding flatbuffers payload.

Wire schema details: [`nng_interface.fbs`](../src/nng_interface/nng_interface.fbs).

---

## Examples

A typical indexer setup in `lotus.conf`:

```
nngrpc=ipc://datadir/nngrpc.pipe
nngpub=ipc://datadir/nngpub.pipe
nngpubmsg=blkconnected
nngpubmsg=blkdisconctd
nngpubmsg=mempooltxadd
nngpubmsg=mempooltxrem
maxapibodysize=2097152
```

### Calling a JSON-RPC over the tunnel (Python / pynng)

```python
import flatbuffers, pynng
from NngInterface import RpcCall, RpcRequest, RestCall, KvPair, RpcResult, RestCallResponse

def make_rest_call(method, path, query=None, body=b"", content_type=""):
    b = flatbuffers.Builder(256)
    method_off       = b.CreateString(method)
    path_off         = b.CreateString(path)
    body_off         = b.CreateByteVector(body)
    content_type_off = b.CreateString(content_type)
    # query vector
    kvs = []
    for k, v in (query or {}).items():
        k_off = b.CreateString(k)
        v_off = b.CreateString(str(v))
        KvPair.Start(b)
        KvPair.AddKey(b, k_off)
        KvPair.AddValue(b, v_off)
        kvs.append(KvPair.End(b))
    RestCall.StartQueryVector(b, len(kvs))
    for off in reversed(kvs):
        b.PrependUOffsetTRelative(off)
    query_vec = b.EndVector()
    RestCall.Start(b)
    RestCall.AddMethod(b, method_off)
    RestCall.AddPath(b, path_off)
    RestCall.AddQuery(b, query_vec)
    RestCall.AddBody(b, body_off)
    RestCall.AddContentType(b, content_type_off)
    rc = RestCall.End(b)
    RpcCall.Start(b)
    RpcCall.AddRpcType(b, RpcRequest.RpcRequest().RestCall)
    RpcCall.AddRpc(b, rc)
    b.Finish(RpcCall.End(b))
    return bytes(b.Output())

with pynng.Req0() as sock:
    sock.dial("ipc:///path/to/nngrpc.pipe")

    # GET /api/v1/rpc/getblockchaininfo
    sock.send(make_rest_call("GET", "rpc/getblockchaininfo"))
    result = RpcResult.RpcResult.GetRootAs(sock.recv(), 0)
    assert result.IsSuccess(), result.ErrorMsg()
    inner = RestCallResponse.RestCallResponse.GetRootAs(
        bytes(result.DataAsNumpy()), 0)
    print(inner.StatusCode(), bytes(inner.BodyAsNumpy()))

    # POST /api/v1/rpc/sendrawtransaction with the hex in the JSON body
    body = b'{"params":["02000000…"]}'
    sock.send(make_rest_call("POST", "rpc/sendrawtransaction",
                             body=body, content_type="application/json"))
```

### Calling a typed endpoint (faster — no path string parsing)

```python
from NngInterface import RpcCall, RpcRequest, GetTxRequest, GetTxResponse

b = flatbuffers.Builder(128)
txid_off = b.CreateString("a"*64)
GetTxRequest.Start(b)
GetTxRequest.AddTxid(b, txid_off)
GetTxRequest.AddVerbose(b, True)
req = GetTxRequest.End(b)
RpcCall.Start(b)
RpcCall.AddRpcType(b, RpcRequest.RpcRequest().GetTxRequest)
RpcCall.AddRpc(b, req)
b.Finish(RpcCall.End(b))
sock.send(bytes(b.Output()))
result = RpcResult.RpcResult.GetRootAs(sock.recv(), 0)
inner = GetTxResponse.GetTxResponse.GetRootAs(bytes(result.DataAsNumpy()), 0)
print(inner.StatusCode(), bytes(inner.BodyAsNumpy()))  # JSON of the tx
```
