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

target_link_libraries(scryptchain common util sqlite-chain)
# libevent must be resolved through the project-wide non_native helper so
# that on cross builds it points to the depends-built static library
# instead of a bare "-levent" that the cross linker can't find.
link_event(scryptchain event)
