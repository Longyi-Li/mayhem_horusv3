#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
codec=mayhem-firmware/firmware/baseband/horus
mkdir -p test-build/sanitized
flags='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
gcc $flags -DNO_TABLES -c "$codec/golay23.c" -o test-build/sanitized/golay.o
gcc $flags -DHORUS_L2_RX -DSCRAMBLER -DINTERLEAVER -c "$codec/horus_l2.c" -o test-build/sanitized/l2.o
g++ $flags -std=c++17 -I"$codec" tests/horus_receiver_host.cpp "$codec/horus_rx.cpp" test-build/sanitized/golay.o test-build/sanitized/l2.o -lm -o test-build/receiver
python3 tests/test_horus.py
