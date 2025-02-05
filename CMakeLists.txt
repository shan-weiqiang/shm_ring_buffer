cmake_minimum_required(VERSION 3.16)

project(shmtest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

add_executable(shmringbuffer_payload test_shmringbuffer_payload.cc)
add_executable(peek_test test_shmringbuffer_payload_peek.cc)

target_link_libraries(shmringbuffer_payload PRIVATE pthread rt)
target_link_libraries(peek_test PRIVATE pthread rt)

