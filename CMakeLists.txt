cmake_minimum_required(VERSION 3.24)
project(concurrent_centralized_database C)

set(CMAKE_C_STANDARD 11)

include_directories(.)

add_executable(concurrent_centralized_database
        client.c
        msg.h
        server.c)
