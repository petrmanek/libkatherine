cmake_minimum_required(VERSION 3.0)
project(katherine LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)

set(CMAKE_CXX_STANDARD 14)

if(UNIX)
    set(CMAKE_CXX_FLAGS "-Wall -Wpedantic -fopenmp ${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native ${CMAKE_CXX_FLAGS_RELEASE}")
endif(UNIX)

# TODO: compiler flags for MSVC

add_subdirectory(c)
add_subdirectory(cxx)
