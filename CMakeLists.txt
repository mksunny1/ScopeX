cmake_minimum_required(VERSION 3.10)
project(ScopeX_Engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(scopex_test test.cpp)
add_executable(scopex_benchmark benchmark.cpp)
target_compile_options(scopex_benchmark PRIVATE -O3 -DNDEBUG)

