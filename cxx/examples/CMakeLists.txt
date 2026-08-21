# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

add_executable(kfindxx kfindxx.cpp address_source.cpp address_source.hpp)
target_link_libraries(kfindxx PRIVATE katherinexx)

add_executable(krunxx krunxx.cpp)
target_link_libraries(krunxx PRIVATE katherinexx)

add_executable(ktempxx ktempxx.cpp)
target_link_libraries(ktempxx PRIVATE katherinexx)
