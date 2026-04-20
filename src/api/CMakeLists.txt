add_library(api
    api_server.cpp
    api_util.cpp
    chain_handler.cpp
    dashboard_handler.cpp
    legacy_rpc_handler.cpp
    tx_handler.cpp
    address_handler.cpp
    network_handler.cpp
    mining_handler.cpp
    events_handler.cpp
    wallet_handler.cpp
    openapi_handler.cpp
    stats_handler.cpp
    overview_handler.cpp
    sitemap_handler.cpp
    stratum_handler.cpp
)

target_link_libraries(api sqlite-chain SQLite::SQLite3 rpcclient)
