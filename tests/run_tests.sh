#!/usr/bin/env bash
# Host unit tests for the pure decoders in components/fpm383c. Needs only g++.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -Wall -Wextra -Werror -o build/test_match_sync test_match_sync.cpp
./build/test_match_sync
