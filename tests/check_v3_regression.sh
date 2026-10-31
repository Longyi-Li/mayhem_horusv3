#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
mkdir -p test-build/v3-baseline
codec=mayhem-firmware/firmware/baseband/horus
g++ -O2 -std=c++17 -Itests/fixtures/v3 -I"$codec" tests/horus_receiver_host.cpp tests/fixtures/v3/horus_rx.cpp test-build/golay.o test-build/l2.o -lm -o test-build/v3-baseline/receiver
python3 tests/test_frequency_regression.py
