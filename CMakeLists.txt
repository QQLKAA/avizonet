cmake_minimum_required(VERSION 3.27)

project(avizonet VERSION 0.0 LANGUAGES CXX)

option(BUILD_EXAMPLES "Build examples")
option(BUILD_TESTS "Build tests")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(libs/cerealizer)

add_library(avizonet
    include/avizonet.h
    src/avizonet.cpp)

target_include_directories(avizonet PUBLIC include)

target_link_libraries(avizonet PUBLIC cerealizer)

if (BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
