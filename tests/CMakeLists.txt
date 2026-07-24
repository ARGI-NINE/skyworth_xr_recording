cmake_minimum_required(VERSION 3.10)
project(fmp4_writer_test CXX)
set(CMAKE_CXX_STANDARD 17)
enable_testing()
add_executable(fmp4_writer_test
    fmp4_writer_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/FMP4Writer.cpp)
target_include_directories(fmp4_writer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp)
add_test(NAME fmp4_writer_regression COMMAND fmp4_writer_test)

add_executable(packet_codec_state_test
    packet_codec_state_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/packet_codec.cpp)
target_include_directories(packet_codec_state_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp)
add_test(NAME packet_codec_state_wire_compat COMMAND packet_codec_state_test)
