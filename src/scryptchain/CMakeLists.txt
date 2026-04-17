add_library(scryptchain
    chain_params.cpp
    header_chain.cpp
    peer_connection.cpp
    network_manager.cpp
    msg_handler.cpp
    mempool.cpp
    template_builder.cpp
    solution_handler.cpp
    scryptchain_globals.cpp
)

target_link_libraries(scryptchain common util event sqlite-chain)
