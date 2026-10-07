#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
FLAGS="-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Isrc"
g++ $FLAGS src/mft.cpp src/main.cpp -o build/mftparse
g++ $FLAGS src/mft.cpp tests/test_mft.cpp -o build/test_mft
echo "build ok"
