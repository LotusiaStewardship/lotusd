add_library(modules
    module_registry.cpp
    builtin_modules.cpp
    rank/rank_module.cpp
)

target_link_libraries(modules sqlite-chain SQLite::SQLite3)
