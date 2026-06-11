# Standalone unit tests for the OBS-free tree logic (src/bsd-tree.cpp).
#
# Deliberately self-contained: no OBS SDK, Qt or plugin scaffolding required, so
# it builds and runs anywhere with just a C++17 compiler + CMake/CTest.
#
#   cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build

cmake_minimum_required(VERSION 3.16)
project(better-scenes-dock-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_executable(bsd-model-tests test-model.cpp "${CMAKE_CURRENT_SOURCE_DIR}/../src/bsd-tree.cpp")
target_include_directories(bsd-model-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

add_test(NAME bsd-model-tests COMMAND bsd-model-tests)
