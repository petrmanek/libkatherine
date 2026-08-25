# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

add_executable(kfindxx kfindxx.cpp address_source.cpp address_source.hpp)
target_link_libraries(kfindxx PRIVATE katherinexx)

add_executable(krunxx krunxx.cpp)
target_link_libraries(krunxx PRIVATE katherinexx)
# The one example written against C++14 (chrono literals). The compile
# feature raises just this target when the build-wide standard is older,
# so a -DCMAKE_CXX_STANDARD=11 build of the C++11 library still succeeds.
target_compile_features(krunxx PRIVATE cxx_std_14)

add_executable(ktempxx ktempxx.cpp)
target_link_libraries(ktempxx PRIVATE katherinexx)

add_executable(kinfoxx kinfoxx.cpp)
target_link_libraries(kinfoxx PRIVATE katherinexx)
