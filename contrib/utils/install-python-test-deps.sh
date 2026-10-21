#!/usr/bin/env bash

# Python packages needed to *run* the test suites:
#   pytest             -- test/functional/test_runner.py
#   pynng, flatbuffers -- the NNG interface the functional framework imports
#                         (test/functional/interface_nng.py)
#
# Split out of install-dependencies.sh on purpose. Building and packaging needs none of these,
# and the test suites now run after the build on a runner with its own Python, so build images
# should not install them. Kept separate rather than deleted because the TeamCity agent runs
# the functional tests in-agent and still needs them.

export LC_ALL=C.UTF-8

set -euxo pipefail

pip3 install pytest pynng flatbuffers
