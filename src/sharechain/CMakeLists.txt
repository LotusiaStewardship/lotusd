# Copyright (c) 2025 The Lotus developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

add_library(sharechain
    share.cpp
    sharechain.cpp
    sharepayout.cpp
    sharevalidation.cpp
)

target_link_libraries(sharechain
    common
    util
)
