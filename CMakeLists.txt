cmake_minimum_required(VERSION 3.20)

project(liquibook-x
    VERSION 0.1.0
    DESCRIPTION "NASDAQ ITCH 5.0 feed handler + low-latency limit order book + matching engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)

option(LIQUIBOOK_BUILD_TESTS "Build GoogleTest-based unit tests" ON)
option(LIQUIBOOK_BUILD_BENCHMARKS "Build Google Benchmark microbenchmarks" OFF)

if(LIQUIBOOK_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    # Match the parent project's runtime library on Windows; irrelevant on Linux/macOS but
    # harmless to set unconditionally.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

if(LIQUIBOOK_BUILD_BENCHMARKS)
    include(FetchContent)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.1
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
endif()

add_subdirectory(smoke)
