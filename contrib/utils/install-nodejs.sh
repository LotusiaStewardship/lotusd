#!/usr/bin/env bash

# Node.js for the cashtab frontend jobs (web/cashtab: npm ci / npm run build / jest).
#
# Split out of install-dependencies.sh because building and packaging lotusd needs no Node at
# all, and installing it costs every build image ~50 MB plus a network round trip through a
# third-party apt bootstrap.
#
# Known broken upstream as of 2026-10: deb.nodesource.com/gpgkey/nodesource.gpg.key now serves
# the NSolid key (2F59B5F99B1BE0B4, nsolid-gpg@nodesource.com), while the node_15.x repository
# is still signed with the legacy key 1655A0AB68576280. apt therefore rejects the repo with
# "The following signatures couldn't be verified ... NO_PUBKEY 1655A0AB68576280". Rebuilds of
# a TeamCity agent will fail here until NodeSource is replaced as the Node source; that is a
# decision for the cashtab jobs, not for the lotusd build.

export LC_ALL=C.UTF-8

set -euxo pipefail

# Install npm v7.x and nodejs v15.x
curl -sL https://deb.nodesource.com/setup_15.x | bash -
apt-get install -y nodejs
