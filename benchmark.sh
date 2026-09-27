#!/bin/bash
set -e

mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make scopex_benchmark
./scopex_benchmark "$@"