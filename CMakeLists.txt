cmake_minimum_required(VERSION 3.12)
project(hypute CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -funroll-loops")

add_library(hypute STATIC src/hypute.cpp)
target_include_directories(hypute PUBLIC include)

add_executable(benchmark bench/benchmark.cpp)
target_link_libraries(benchmark hypute)

# Standalone latency micro-benchmark (hardware-gated sparse traversal vs dense scan)
add_executable(gating_benchmark bench/gating_benchmark.cpp)

add_executable(quickstart examples/quickstart.cpp)
target_link_libraries(quickstart hypute)

enable_testing()
add_executable(hypute_tests tests/test.cpp)
target_link_libraries(hypute_tests hypute)
add_test(NAME hypute_tests COMMAND hypute_tests)
