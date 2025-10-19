
cmake_minimum_required(VERSION 3.28)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(mpsc example/mpsc.cc)
target_include_directories(mpsc 
    PUBLIC 
        .
)
