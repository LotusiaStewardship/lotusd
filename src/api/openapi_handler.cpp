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
        get.pushKV("summary", "Get chain information");
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
        get.pushKV("summary", "Get chain tip");
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
        get.pushKV("summary", "Get mempool info and transactions");
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
        get.pushKV("summary", "Rich list (addresses ranked by balance)");
        get.pushKV("operationId", "listAddresses");
        UniValue tags(UniValue::VARR);
        tags.push_back("Addresses");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(QueryParam("limit", "Max results", "integer"));
        params.push_back(QueryParam("offset", "Offset", "integer"));
        get.pushKV("parameters", params);
        UniValue responses(UniValue::VOBJ);
        responses.pushKV("200", JsonResponse("Address list", SchemaRef("PaginatedAddresses")));
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
        get.pushKV("summary", "Get network information");
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
        get.pushKV("summary", "List connected peers");
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
        get.pushKV("summary", "Get node information");
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
        get.pushKV("summary", "Get mining information");
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

    // /events
    {
        UniValue get(UniValue::VOBJ);
        get.pushKV("summary", "Poll chain events (long-poll style)");
        get.pushKV("operationId", "getEvents");
        UniValue tags(UniValue::VARR);
        tags.push_back("Events");
        get.pushKV("tags", tags);
        UniValue params(UniValue::VARR);
        params.push_back(QueryParam("since_seq", "Return events after this sequence number", "integer"));
        params.push_back(QueryParam("limit", "Max events to return", "integer"));
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
                "RESTful API for the Lotus blockchain node, powered by SQLite");
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
