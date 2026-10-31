#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
mkdir -p test-build
codec=mayhem-firmware/firmware/baseband/horus
gcc -g -O2 -fPIC -DNO_TABLES -c "$codec/golay23.c" -o test-build/golay.o
gcc -g -O2 -fPIC -DHORUS_L2_RX -DSCRAMBLER -DINTERLEAVER -c "$codec/horus_l2.c" -o test-build/l2.o
gcc -shared test-build/golay.o test-build/l2.o -lm -o test-build/codec.so
g++ -g -O2 -Wall -Wextra -Werror -std=c++17 -I"$codec" tests/horus_receiver_host.cpp "$codec/horus_rx.cpp" test-build/golay.o test-build/l2.o -lm -o test-build/receiver
