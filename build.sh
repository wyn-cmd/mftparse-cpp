#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
FLAGS="-std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Isrc"
SRC="src/mft.cpp src/render.cpp src/mapped_file.cpp"
g++ $FLAGS -pthread $SRC src/main.cpp -o build/mftparse
g++ $FLAGS -pthread $SRC tests/test_mft.cpp -o build/test_mft
echo "build ok"
