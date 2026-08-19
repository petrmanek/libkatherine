# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

add_executable(kfind kfind.c)
target_link_libraries(kfind katherine)

add_executable(krun krun.c)
target_link_libraries(krun katherine)
