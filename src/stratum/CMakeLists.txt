# Copyright (c) 2025 The Lotus developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

add_library(stratum
    stratum.cpp
    stratumconfig.cpp
    stratumjob.cpp
    stratumprotocol.cpp
    stratumproxy.cpp
    stratumrouter.cpp
    stratumstats.cpp
    stratumsubmit.cpp
    stratumworker.cpp
)

target_link_libraries(stratum
    common
    util
    event
)
