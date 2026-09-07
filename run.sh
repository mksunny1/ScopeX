#!/bin/bash

# Exit immediately if any compilation or test command fails (returns non-zero)
set -e

echo "=== Creating Build Directory ==="
mkdir -p build
cd build

echo "=== Running CMake Configuration ==="
cmake ..

echo "=== Compiling ScopeX Engine ==="
make

echo "=== Executing Verification Tests ==="
echo ""
./scopex_test
