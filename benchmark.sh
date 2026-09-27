#!/bin/bash

cmake -DCMAKE_BUILD_TYPE=Release ..
make scopex_benchmark
./scopex_benchmark

