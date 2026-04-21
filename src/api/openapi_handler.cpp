// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/openapi_handler.h>

#include <clientversion.h>
#include <rpc/protocol.h>

namespace api {

static UniValue SchemaRef(const std::string &name) {
    UniValue ref(UniValue::VOBJ);
    ref.pushKV("$ref", "#/components/schemas/" + name);
    return ref;
}

static UniValue StringSchema() {
    UniValue s(UniValue::VOBJ);
    s.pushKV("type", "string");
    return s;
}

static UniValue IntSchema() {
    UniValue s(UniValue::VOBJ);
    s.pushKV("type", "integer");
    return s;
}

static UniValue BoolSchema() {
    UniValue s(UniValue::VOBJ);
    s.pushKV("type", "boolean");
    return s;
}

static UniValue NumSchema() {
    UniValue s(UniValue::VOBJ);
    s.pushKV("type", "number");
    return s;
}

static UniValue QueryParam(const std::string &name, const std::string &desc,
                            const std::string &type, bool required = false) {
    UniValue p(UniValue::VOBJ);
    p.pushKV("name", name);
    p.pushKV("in", "query");
    p.pushKV("description", desc);
    p.pushKV("required", required);
    UniValue schema(UniValue::VOBJ);
    schema.pushKV("type", type);
    p.pushKV("schema", schema);
    return p;
}

static UniValue PathParam(const std::string &name, const std::string &desc,
                           const std::string &type) {
    UniValue p(UniValue::VOBJ);
    p.pushKV("name", name);
    p.pushKV("in", "path");
    p.pushKV("description", desc);
    p.pushKV("required", true);
    UniValue schema(UniValue::VOBJ);
    schema.pushKV("type", type);
    p.pushKV("schema", schema);
    return p;
}

static UniValue JsonResponse(const std::string &desc,
                               const UniValue &schema) {
    UniValue resp(UniValue::VOBJ);
    resp.pushKV("description", desc);
    UniValue content(UniValue::VOBJ);
    UniValue json(UniValue::VOBJ);
    json.pushKV("schema", schema);
    content.pushKV("application/json", json);
    resp.pushKV("content", content);
    return resp;
}

static UniValue BuildPaths() {
    UniValue paths(UniValue::VOBJ);

    // /chain
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get blockchain status and metadata");
        get.pushKV("description",
            "Returns the current chain name, height, best block hash, "
            "difficulty, median time, cumulative chain work, and whether "
            "the node is still in initial block download (IBD).");
        get.pushKV("operationId", "getChainInfo");
        UniValue tags(UniValue::VARR);
        tags.push_back("Chain");
        get.pushKV("tags", tags);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Chain info", SchemaRef("ChainInfo")));
        get.pushKV("responses", responses);

        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/chain", path);
    }

    // /chain/tip
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get the latest block (chain tip)");
        get.pushKV("description",
            "Returns a summary of the most recently mined block, including "
            "hash, height, timestamp, transaction count, and size.");
        get.pushKV("operationId", "getChainTip");
        UniValue tags(UniValue::VARR);
        tags.push_back("Chain");
        get.pushKV("tags", tags);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Chain tip", SchemaRef("BlockSummary")));
        get.pushKV("responses", responses);

        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/chain/tip", path);
    }

    // /blocks
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "List recent blocks");
        get.pushKV("description",
            "Returns a paginated list of blocks from the tip backwards. "
            "Each entry includes hash, height, timestamp, tx count, size, "
            "difficulty, and confirmations.");
        get.pushKV("operationId", "listBlocks");
        UniValue tags(UniValue::VARR);
        tags.push_back("Blocks");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(QueryParam("limit", "Max results (1-100)", "integer"));
        params.push_back(QueryParam("offset", "Skip N blocks from tip", "integer"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Block list", SchemaRef("PaginatedBlocks")));
        get.pushKV("responses", responses);

        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/blocks", path);
    }

    // /blocks/{id}
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get block by hash or height");
        get.pushKV("description",
            "Accepts either a block hash (64 hex chars) or a decimal height. "
            "Returns full block summary including coinbase, merkle root, and "
            "transaction count.");
        get.pushKV("operationId", "getBlock");
        UniValue tags(UniValue::VARR);
        tags.push_back("Blocks");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(PathParam("id", "Block hash or height", "string"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Block details", SchemaRef("BlockSummary")));
        get.pushKV("responses", responses);

        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/blocks/{id}", path);
    }

    // /blocks/{id}/txs
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get transactions in a block");
        get.pushKV("operationId", "getBlockTxs");
        UniValue tags(UniValue::VARR);
        tags.push_back("Blocks");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(PathParam("id", "Block hash or height", "string"));
        params.push_back(QueryParam("limit", "Max results", "integer"));
        params.push_back(QueryParam("offset", "Offset", "integer"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Block txs", SchemaRef("PaginatedTxRefs")));
        get.pushKV("responses", responses);

        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/blocks/{id}/txs", path);
    }

    // /txs/{txid}
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get transaction by txid");
        get.pushKV("description",
            "Returns transaction details including inputs, outputs, fees, "
            "block height, confirmations, and OP_RETURN data if present.");
        get.pushKV("operationId", "getTx");
        UniValue tags(UniValue::VARR);
        tags.push_back("Transactions");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(PathParam("txid", "Transaction ID (64 hex chars)", "string"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Tx summary", SchemaRef("TxSummary")));
        get.pushKV("responses", responses);

        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/txs/{txid}", path);
    }

    // /txs/{txid}/inputs, /txs/{txid}/outputs
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get transaction inputs");
        get.pushKV("operationId", "getTxInputs");
        UniValue tags(UniValue::VARR);
        tags.push_back("Transactions");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(PathParam("txid", "Transaction ID", "string"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Inputs array", SchemaRef("TxInputArray")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/txs/{txid}/inputs", path);
    }
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get transaction outputs");
        get.pushKV("operationId", "getTxOutputs");
        UniValue tags(UniValue::VARR);
        tags.push_back("Transactions");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(PathParam("txid", "Transaction ID", "string"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Outputs array", SchemaRef("TxOutputArray")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/txs/{txid}/outputs", path);
    }

    // /mempool
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get mempool info and pending transactions");
        get.pushKV("description",
            "Returns current mempool size, byte total, and a list of "
            "unconfirmed transaction IDs with their fee rates and sizes.");
        get.pushKV("operationId", "getMempool");
        UniValue tags(UniValue::VARR);
        tags.push_back("Mempool");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(QueryParam("limit", "Max transactions (1-500)", "integer"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Mempool info", SchemaRef("MempoolInfo")));
        get.pushKV("responses", responses);

        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/mempool", path);
    }

    // /txs/send, /txs/decode
    {
        UniValue post(UniValue::VOBJ);
        post.pushKV("summary", "Submit a raw transaction");
        post.pushKV("operationId", "sendTx");
        UniValue tags(UniValue::VARR);
        tags.push_back("Transactions");
        post.pushKV("tags", tags);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("201", JsonResponse("Accepted tx", SchemaRef("SendTxResult")));
        post.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("post", post);
        paths.pushKV("/api/v1/txs/send", path);
    }
    {
        UniValue post(UniValue::VOBJ);
        post.pushKV("summary", "Decode a raw transaction");
        post.pushKV("operationId", "decodeTx");
        UniValue tags(UniValue::VARR);
        tags.push_back("Transactions");
        post.pushKV("tags", tags);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Decoded tx", SchemaRef("DecodedTx")));
        post.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("post", post);
        paths.pushKV("/api/v1/txs/decode", path);
    }

    // /addresses, /addresses/{addr}, /addresses/{addr}/txs, /addresses/{addr}/utxos
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Rich list / wealth distribution");
        get.pushKV("description",
            "Without query params, returns addresses ranked by balance "
            "(descending). Use ?sort=received to rank by total received. "
            "Use ?mode=wealth to get wealth distribution buckets instead "
            "of individual addresses — returns counts of addresses in "
            "ranges like 0-1 XPI, 1-10 XPI, etc.");
        get.pushKV("operationId", "listAddresses");
        UniValue tags(UniValue::VARR);
        tags.push_back("Addresses");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(QueryParam("limit", "Max results (default 20, max 100)", "integer"));
        params.push_back(QueryParam("offset", "Skip N entries for pagination", "integer"));
        params.push_back(QueryParam("sort",
            "Sort order: 'balance' (default) or 'received'", "string"));
        params.push_back(QueryParam("mode",
            "Set to 'wealth' to return wealth distribution buckets "
            "instead of individual addresses", "string"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Address list or wealth distribution", SchemaRef("PaginatedAddresses")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/addresses", path);
    }
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get address summary");
        get.pushKV("operationId", "getAddress");
        UniValue tags(UniValue::VARR);
        tags.push_back("Addresses");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(PathParam("address", "Lotus address", "string"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Address info", SchemaRef("AddressSummary")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/addresses/{address}", path);
    }
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get address transaction history");
        get.pushKV("operationId", "getAddressTxs");
        UniValue tags(UniValue::VARR);
        tags.push_back("Addresses");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(PathParam("address", "Lotus address", "string"));
        params.push_back(QueryParam("limit", "Max results", "integer"));
        params.push_back(QueryParam("offset", "Offset", "integer"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Tx history", SchemaRef("PaginatedTxHistory")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/addresses/{address}/txs", path);
    }
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get address unspent outputs");
        get.pushKV("operationId", "getAddressUtxos");
        UniValue tags(UniValue::VARR);
        tags.push_back("Addresses");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(PathParam("address", "Lotus address", "string"));
        params.push_back(QueryParam("limit", "Max results", "integer"));
        params.push_back(QueryParam("offset", "Offset", "integer"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("UTXOs", SchemaRef("PaginatedUtxos")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/addresses/{address}/utxos", path);
    }

    // /utxos/{txid}/{vout}
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Check specific UTXO");
        get.pushKV("operationId", "getUtxo");
        UniValue tags(UniValue::VARR);
        tags.push_back("UTXOs");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(PathParam("txid", "Transaction ID", "string"));
        params.push_back(PathParam("vout", "Output index", "integer"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("UTXO details", SchemaRef("UtxoDetail")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/utxos/{txid}/{vout}", path);
    }

    // /network, /network/peers, /node
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get network status and connection counts");
        get.pushKV("description",
            "Returns protocol version, user agent, inbound/outbound "
            "connection counts, and whether the network is active.");
        get.pushKV("operationId", "getNetworkInfo");
        UniValue tags(UniValue::VARR);
        tags.push_back("Network");
        get.pushKV("tags", tags);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Network info", SchemaRef("NetworkInfo")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/network", path);
    }
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "List all connected peers with stats");
        get.pushKV("description",
            "Returns per-peer details: address, user agent, direction "
            "(inbound/outbound), starting height, ping latency, and "
            "bytes sent/received.");
        get.pushKV("operationId", "getPeers");
        UniValue tags(UniValue::VARR);
        tags.push_back("Network");
        get.pushKV("tags", tags);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Peer list", SchemaRef("PeerArray")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/network/peers", path);
    }
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get node software version and uptime");
        get.pushKV("description",
            "Returns software version, protocol version, uptime in seconds, "
            "IBD status, current chain height, and data directory path.");
        get.pushKV("operationId", "getNodeInfo");
        UniValue tags(UniValue::VARR);
        tags.push_back("Node");
        get.pushKV("tags", tags);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Node info", SchemaRef("NodeInfo")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/node", path);
    }

    // /mining
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get current mining difficulty and chain info");
        get.pushKV("description",
            "Returns the current difficulty, target bits, chain name, and "
            "tip height. Useful for miners and pool operators.");
        get.pushKV("operationId", "getMiningInfo");
        UniValue tags(UniValue::VARR);
        tags.push_back("Mining");
        get.pushKV("tags", tags);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Mining info", SchemaRef("MiningInfo")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/mining", path);
    }

    // /stats/cards
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Live aggregate stats for dashboard cards");
        get.pushKV("description",
            "Returns current tip height, difficulty, estimated hashrate, "
            "mempool size, total circulating supply, and burned sats. "
            "Data is computed live from the chain state and SQLite indexes.");
        get.pushKV("operationId", "getStatsCards");
        UniValue tags(UniValue::VARR);
        tags.push_back("Stats");
        get.pushKV("tags", tags);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Stats cards", SchemaRef("StatsCards")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/stats/cards", path);
    }

    // /stats/charts
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Historical time-series data for charts");
        get.pushKV("description",
            "Returns an array of snapshots taken every 5 minutes, with "
            "hashrate, difficulty, mempool size, supply, and burned supply. "
            "On first start, historical data is backfilled from the block "
            "index (one sample per hour). Use the 'period' parameter to "
            "control the time window.");
        get.pushKV("operationId", "getStatsCharts");
        UniValue tags(UniValue::VARR);
        tags.push_back("Stats");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(QueryParam("period",
            "Time window: day (default, 288 points), week, month, quarter, "
            "year, or 7d/30d/90d/365d aliases", "string"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Chart series", SchemaRef("StatsChartSeries")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/stats/charts", path);
    }

    // /overview
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Combined overview of chain, mining, mempool, and network");
        get.pushKV("description",
            "Single endpoint that aggregates chain info, latest block "
            "summary, mining stats, mempool status, and network peer counts. "
            "Useful for dashboard landing pages to avoid multiple round-trips.");
        get.pushKV("operationId", "getOverview");
        UniValue tags(UniValue::VARR);
        tags.push_back("Overview");
        get.pushKV("tags", tags);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Overview", SchemaRef("Overview")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/overview", path);
    }

    // /mempool/history
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Mempool size history over time");
        get.pushKV("description",
            "Returns time-series snapshots of mempool tx count and total "
            "bytes. Snapshots are recorded every 5 minutes by the stats "
            "collector. Useful for mempool congestion charts.");
        get.pushKV("operationId", "getMempoolHistory");
        UniValue tags(UniValue::VARR);
        tags.push_back("Mempool");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(QueryParam("period",
            "Time window: day (default), week, month, quarter, year",
            "string"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Mempool history", SchemaRef("MempoolHistorySeries")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/mempool/history", path);
    }

    // /network/nodes
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get connected peer addresses as addnode/onetry strings");
        get.pushKV("description",
            "Returns the currently connected peers formatted as 'addnode=' "
            "and 'onetry=' strings, ready to be used in lotus.conf or CLI. "
            "Useful for bootstrapping new nodes.");
        get.pushKV("operationId", "getNetworkNodes");
        UniValue tags(UniValue::VARR);
        tags.push_back("Network");
        get.pushKV("tags", tags);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Node strings", SchemaRef("NetworkNodes")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/network/nodes", path);
    }

    // /events
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Poll chain events (long-poll style)");
        get.pushKV("description",
            "Long-poll endpoint for real-time chain events. Returns new "
            "blocks, transactions, and other events since the given sequence "
            "number. The client should poll with the last received seq to "
            "get incremental updates. Times out after ~30s if no events.");
        get.pushKV("operationId", "getEvents");
        UniValue tags(UniValue::VARR);
        tags.push_back("Events");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(QueryParam("since_seq", "Return events after this sequence number", "integer"));
        params.push_back(QueryParam("limit", "Max events to return (default 50)", "integer"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Events", SchemaRef("EventList")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/events", path);
    }

    // /wallet
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Get wallet info (requires wallet support)");
        get.pushKV("description",
            "Returns wallet balance and status. Only available when the node "
            "is compiled with wallet support and a wallet is loaded.");
        get.pushKV("operationId", "getWalletInfo");
        UniValue tags(UniValue::VARR);
        tags.push_back("Wallet");
        get.pushKV("tags", tags);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Wallet info", SchemaRef("WalletInfo")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/wallet", path);
    }

    // /addresses/batch/{summary,utxos,txs}
    {
        auto buildBatchPath =
            [&](const std::string &op, const std::string &summary,
                const std::string &description, const std::string &itemRef,
                bool itemIsArray) {
                UniValue post(UniValue::VOBJ);
                post.pushKV("summary", summary);
                post.pushKV("description", description);
                post.pushKV("operationId", op);
                UniValue tags(UniValue::VARR);
                tags.push_back("Address");
                post.pushKV("tags", tags);

                UniValue body(UniValue::VOBJ);
                body.pushKV("required", true);
                UniValue content(UniValue::VOBJ);
                UniValue json(UniValue::VOBJ);
                UniValue bodySchema(UniValue::VOBJ);
                bodySchema.pushKV("type", "object");
                UniValue bodyProps(UniValue::VOBJ);
                UniValue addrArr(UniValue::VOBJ);
                addrArr.pushKV("type", "array");
                addrArr.pushKV("items", StringSchema());
                addrArr.pushKV("maxItems", 100);
                bodyProps.pushKV("addresses", addrArr);
                if (op == "batchAddressTxs") {
                    bodyProps.pushKV("limit", IntSchema());
                    bodyProps.pushKV("offset", IntSchema());
                }
                bodySchema.pushKV("properties", bodyProps);
                UniValue required(UniValue::VARR);
                required.push_back("addresses");
                bodySchema.pushKV("required", required);
                json.pushKV("schema", bodySchema);
                content.pushKV("application/json", json);
                body.pushKV("content", content);
                post.pushKV("requestBody", body);

                UniValue respSchema(UniValue::VOBJ);
                respSchema.pushKV("type", "object");
                UniValue respProps(UniValue::VOBJ);
                UniValue dataMap(UniValue::VOBJ);
                dataMap.pushKV("type", "object");
                if (itemIsArray) {
                    UniValue valArr(UniValue::VOBJ);
                    valArr.pushKV("type", "array");
                    valArr.pushKV("items", SchemaRef(itemRef));
                    dataMap.pushKV("additionalProperties", valArr);
                } else {
                    dataMap.pushKV("additionalProperties", SchemaRef(itemRef));
                }
                respProps.pushKV("data", dataMap);
                respSchema.pushKV("properties", respProps);

                UniValue responses(UniValue::VOBJ);
                responses.pushKV("200",
                                  JsonResponse("Per-address payloads keyed "
                                               "by address",
                                               respSchema));
                responses.pushKV("400",
                                  JsonResponse("Invalid request body",
                                               SchemaRef("Error")));
                post.pushKV("responses", responses);

                UniValue path(UniValue::VOBJ);
                path.pushKV("post", post);
                return path;
            };

        paths.pushKV("/api/v1/addresses/batch/summary",
                     buildBatchPath("batchAddressSummary",
                                    "Batch address summaries",
                                    "Returns the singleton "
                                    "/addresses/<addr> payload for up to "
                                    "100 addresses in a single request. "
                                    "Unknown addresses are returned as "
                                    "zeroed records (never 404).",
                                    "AddressSummary",
                                    /*itemIsArray=*/false));
        paths.pushKV("/api/v1/addresses/batch/utxos",
                     buildBatchPath("batchAddressUtxos",
                                    "Batch address UTXOs",
                                    "Returns the singleton "
                                    "/addresses/<addr>/utxos array for up "
                                    "to 100 addresses. The per-address "
                                    "limit (default 50, max 500) is taken "
                                    "from the `limit` query parameter.",
                                    "AddressUtxo",
                                    /*itemIsArray=*/true));
        paths.pushKV("/api/v1/addresses/batch/txs",
                     buildBatchPath("batchAddressTxs",
                                    "Batch address transactions",
                                    "Returns the singleton "
                                    "/addresses/<addr>/txs array for up to "
                                    "100 addresses. Per-address `limit` "
                                    "(default 25, max 200) and `offset` "
                                    "may be supplied either via query "
                                    "string or via the JSON body.",
                                    "AddressTx",
                                    /*itemIsArray=*/true));
    }

    // /health
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Lightweight liveness probe");
        get.pushKV("description",
            "Cheap liveness check intended for browsers and light wallets. "
            "Always returns 200 once the chain is loaded; the `ibd` flag "
            "indicates whether the node is still doing initial block "
            "download. Returns 503 only when no tip is available yet.");
        get.pushKV("operationId", "getHealth");
        UniValue tags(UniValue::VARR);
        tags.push_back("Node");
        get.pushKV("tags", tags);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Health snapshot",
                                              SchemaRef("HealthInfo")));
        responses.pushKV("503", JsonResponse("Chain not yet loaded",
                                              SchemaRef("Error")));
        get.pushKV("responses", responses);
        UniValue path(UniValue::VOBJ);
        path.pushKV("get", get);
        paths.pushKV("/api/v1/health", path);
    }

    return paths;
}

static UniValue BuildSchemas() {
    UniValue schemas(UniValue::VOBJ);

    // Error
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        UniValue props(UniValue::VOBJ);
        props.pushKV("error", StringSchema());
        props.pushKV("message", StringSchema());
        props.pushKV("status", IntSchema());
        s.pushKV("properties", props);
        schemas.pushKV("ApiError", s);
    }

    // Pagination
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        UniValue props(UniValue::VOBJ);
        props.pushKV("total", IntSchema());
        props.pushKV("limit", IntSchema());
        props.pushKV("offset", IntSchema());
        props.pushKV("has_more", BoolSchema());
        s.pushKV("properties", props);
        schemas.pushKV("Pagination", s);
    }

    // BlockSummary
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        UniValue props(UniValue::VOBJ);
        props.pushKV("hash", StringSchema());
        props.pushKV("height", IntSchema());
        props.pushKV("time", IntSchema());
        props.pushKV("n_tx", IntSchema());
        props.pushKV("size", IntSchema());
        props.pushKV("difficulty", NumSchema());
        props.pushKV("confirmations", IntSchema());
        props.pushKV("previous_hash", StringSchema());
        s.pushKV("properties", props);
        schemas.pushKV("BlockSummary", s);
    }

    // ChainInfo
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        UniValue props(UniValue::VOBJ);
        props.pushKV("chain", StringSchema());
        props.pushKV("height", IntSchema());
        props.pushKV("best_block_hash", StringSchema());
        props.pushKV("difficulty", NumSchema());
        props.pushKV("median_time", IntSchema());
        props.pushKV("chain_work", StringSchema());
        props.pushKV("initial_block_download", BoolSchema());
        s.pushKV("properties", props);
        schemas.pushKV("ChainInfo", s);
    }

    // TxSummary
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        UniValue props(UniValue::VOBJ);
        props.pushKV("txid", StringSchema());
        props.pushKV("block_height", IntSchema());
        props.pushKV("block_pos", IntSchema());
        props.pushKV("confirmations", IntSchema());
        props.pushKV("input_count", IntSchema());
        props.pushKV("output_count", IntSchema());
        props.pushKV("input_value_sats", IntSchema());
        props.pushKV("output_value_sats", IntSchema());
        props.pushKV("fee_sats", IntSchema());
        s.pushKV("properties", props);
        schemas.pushKV("TxSummary", s);
    }

    // AddressSummary
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        s.pushKV("description", "Balance, activity, and UTXO summary for a single address");
        UniValue props(UniValue::VOBJ);
        props.pushKV("address", StringSchema());
        props.pushKV("balance_sats", IntSchema());
        props.pushKV("received_sats", IntSchema());
        props.pushKV("sent_sats", IntSchema());
        props.pushKV("tx_count", IntSchema());
        props.pushKV("utxo_count", IntSchema());
        props.pushKV("first_height", IntSchema());
        props.pushKV("last_height", IntSchema());
        s.pushKV("properties", props);
        schemas.pushKV("AddressSummary", s);
    }

    // StatsCards — live aggregate snapshot
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        s.pushKV("description", "Live aggregate stats for dashboard cards");
        UniValue props(UniValue::VOBJ);
        props.pushKV("tip_height", IntSchema());
        props.pushKV("difficulty", NumSchema());
        props.pushKV("hashrate", NumSchema());
        props.pushKV("mempool_count", IntSchema());
        props.pushKV("mempool_bytes", IntSchema());
        props.pushKV("total_supply_sats", IntSchema());
        props.pushKV("burned_sats", IntSchema());
        s.pushKV("properties", props);
        schemas.pushKV("StatsCards", s);
    }

    // StatsChartPoint — single time-series data point
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        s.pushKV("description", "A single snapshot in a time-series chart");
        UniValue props(UniValue::VOBJ);
        props.pushKV("ts", IntSchema());
        props.pushKV("block_height", IntSchema());
        props.pushKV("hashrate", NumSchema());
        props.pushKV("difficulty", NumSchema());
        props.pushKV("mempool_count", IntSchema());
        props.pushKV("total_supply_sats", IntSchema());
        props.pushKV("burned_supply_sats", IntSchema());
        s.pushKV("properties", props);
        schemas.pushKV("StatsChartPoint", s);
    }

    // StatsChartSeries — time-series wrapper
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        s.pushKV("description",
            "Time-series chart data with period label and array of snapshots "
            "ordered oldest-first. Snapshots are taken every 5 minutes; "
            "historical data is backfilled hourly from the block index.");
        UniValue props(UniValue::VOBJ);
        props.pushKV("period", StringSchema());
        UniValue seriesArr(UniValue::VOBJ);
        seriesArr.pushKV("type", "array");
        seriesArr.pushKV("items", SchemaRef("StatsChartPoint"));
        props.pushKV("series", seriesArr);
        s.pushKV("properties", props);
        schemas.pushKV("StatsChartSeries", s);
    }

    // Overview — combined dashboard response
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        s.pushKV("description",
            "Combined response with chain, mining, mempool, network, and "
            "latest block info. Avoids multiple round-trips for dashboard "
            "rendering.");
        UniValue props(UniValue::VOBJ);
        props.pushKV("chain", SchemaRef("ChainInfo"));
        props.pushKV("latest_block", SchemaRef("BlockSummary"));
        UniValue miningObj(UniValue::VOBJ);
        miningObj.pushKV("type", "object");
        props.pushKV("mining", miningObj);
        UniValue mempoolObj(UniValue::VOBJ);
        mempoolObj.pushKV("type", "object");
        props.pushKV("mempool", mempoolObj);
        UniValue netObj(UniValue::VOBJ);
        netObj.pushKV("type", "object");
        props.pushKV("network", netObj);
        s.pushKV("properties", props);
        schemas.pushKV("Overview", s);
    }

    // MempoolHistoryPoint
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        s.pushKV("description", "A single mempool snapshot");
        UniValue props(UniValue::VOBJ);
        props.pushKV("ts", IntSchema());
        props.pushKV("tx_count", IntSchema());
        props.pushKV("total_bytes", IntSchema());
        s.pushKV("properties", props);
        schemas.pushKV("MempoolHistoryPoint", s);
    }

    // MempoolHistorySeries
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        s.pushKV("description",
            "Time-series of mempool size snapshots, oldest-first. "
            "Useful for plotting mempool congestion over time.");
        UniValue props(UniValue::VOBJ);
        props.pushKV("period", StringSchema());
        UniValue seriesArr(UniValue::VOBJ);
        seriesArr.pushKV("type", "array");
        seriesArr.pushKV("items", SchemaRef("MempoolHistoryPoint"));
        props.pushKV("series", seriesArr);
        s.pushKV("properties", props);
        schemas.pushKV("MempoolHistorySeries", s);
    }

    // NetworkNodes
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        s.pushKV("description",
            "Currently connected peers formatted as addnode= and onetry= "
            "strings for lotus.conf or CLI usage");
        UniValue props(UniValue::VOBJ);
        UniValue addArr(UniValue::VOBJ);
        addArr.pushKV("type", "array");
        addArr.pushKV("items", StringSchema());
        props.pushKV("addnode", addArr);
        UniValue oneArr(UniValue::VOBJ);
        oneArr.pushKV("type", "array");
        oneArr.pushKV("items", StringSchema());
        props.pushKV("onetry", oneArr);
        s.pushKV("properties", props);
        schemas.pushKV("NetworkNodes", s);
    }

    // AddressSummary — single row of address_balances (matches GET
    // /api/v1/addresses/<addr> and the batch summary payload).
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        UniValue props(UniValue::VOBJ);
        props.pushKV("address", StringSchema());
        props.pushKV("balance_sats", IntSchema());
        props.pushKV("received_sats", IntSchema());
        props.pushKV("sent_sats", IntSchema());
        props.pushKV("tx_count", IntSchema());
        props.pushKV("utxo_count", IntSchema());
        props.pushKV("first_height", IntSchema());
        props.pushKV("last_height", IntSchema());
        s.pushKV("properties", props);
        schemas.pushKV("AddressSummary", s);
    }

    // AddressUtxo — single unspent output owned by an address.
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        UniValue props(UniValue::VOBJ);
        props.pushKV("txid", StringSchema());
        props.pushKV("vout", IntSchema());
        props.pushKV("value_sats", IntSchema());
        s.pushKV("properties", props);
        schemas.pushKV("AddressUtxo", s);
    }

    // AddressTx — single row of address_history.
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        UniValue props(UniValue::VOBJ);
        props.pushKV("block_height", IntSchema());
        props.pushKV("txid", StringSchema());
        props.pushKV("net_value", IntSchema());
        s.pushKV("properties", props);
        schemas.pushKV("AddressTx", s);
    }

    // HealthInfo
    {
        UniValue s(UniValue::VOBJ);
        s.pushKV("type", "object");
        s.pushKV("description",
            "Liveness snapshot returned by GET /api/v1/health. Cheap to "
            "compute and cached for one second.");
        UniValue props(UniValue::VOBJ);
        UniValue statusEnum(UniValue::VOBJ);
        statusEnum.pushKV("type", "string");
        UniValue values(UniValue::VARR);
        values.push_back("ok");
        values.push_back("syncing");
        statusEnum.pushKV("enum", values);
        props.pushKV("status", statusEnum);
        props.pushKV("height", IntSchema());
        props.pushKV("best_block_hash", StringSchema());
        props.pushKV("median_time", IntSchema());
        props.pushKV("ibd", BoolSchema());
        props.pushKV("uptime_seconds", IntSchema());
        props.pushKV("version", IntSchema());
        props.pushKV("subversion", StringSchema());
        s.pushKV("properties", props);
        schemas.pushKV("HealthInfo", s);
    }

    return schemas;
}

bool HandleGetOpenAPISchema(const util::Ref &, HTTPRequest *req,
                            const std::vector<std::string> &,
                            const QueryParams &) {
    UniValue doc(UniValue::VOBJ);
    doc.pushKV("openapi", "3.1.0");

    UniValue info(UniValue::VOBJ);
    info.pushKV("title", "Lotus Node API");
    info.pushKV("description",
                "RESTful API for the Lotus blockchain node. All data is served "
                "directly from the node's embedded SQLite database and in-memory "
                "state — no external indexer required. Supports chain info, block "
                "and transaction queries, address balances with rich list, mempool "
                "monitoring with history, mining stats, network peers, historical "
                "charts (hashrate, difficulty, supply), real-time events via "
                "long-polling, and a combined overview endpoint for dashboards.");
    info.pushKV("version", FormatFullVersion());
    doc.pushKV("info", info);

    UniValue servers(UniValue::VARR);
    UniValue server(UniValue::VOBJ);
    server.pushKV("url", "/api/v1");
    server.pushKV("description", "This node");
    servers.push_back(server);
    doc.pushKV("servers", servers);

    doc.pushKV("paths", BuildPaths());

    UniValue components(UniValue::VOBJ);
    components.pushKV("schemas", BuildSchemas());
    doc.pushKV("components", components);

    WriteSuccess(req, doc);
    return true;
}

} // namespace api
