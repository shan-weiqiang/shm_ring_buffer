cmake_minimum_required(VERSION 3.16)

project(shmtest)

add_executable(shmringbuffer_payload test_shmringbuffer_payload.cc)

target_link_libraries(shmringbuffer_payload PRIVATE pthread rt)

