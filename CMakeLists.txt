project(logger)
cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)

add_executable(mpsc example/mpsc.cc)
target_include_directories(mpsc 
    PUBLIC 
        .
)

target_link_options(mpsc PUBLIC "-pthread")