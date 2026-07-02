cmake_minimum_required(VERSION 3.10)
project(fmp4_writer_test CXX)
set(CMAKE_CXX_STANDARD 17)
add_executable(fmp4_writer_test
    fmp4_writer_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/FMP4Writer.cpp)
target_include_directories(fmp4_writer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp)
