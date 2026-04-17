# Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero

project(sqlite-chain)

# Reuse the SQLite3 already found by the wallet module, or find it ourselves.
if(NOT TARGET SQLite::SQLite3)
    find_package(SQLite3 3.7.17 REQUIRED)
endif()

add_library(sqlite-chain
    schema.cpp
    sqlite_wrapper.cpp
    coins_view_sqlite.cpp
    block_tree_sqlite.cpp
    address_index.cpp
    block_analytics.cpp
)

target_include_directories(sqlite-chain PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sqlite-chain common bitcoinconsensus SQLite::SQLite3)
