cmake_minimum_required(VERSION 3.5.0)
project(allocator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

add_executable(allocator allocator.cpp tlsf_allocator.cpp memory_visualization.cpp main.cpp)

