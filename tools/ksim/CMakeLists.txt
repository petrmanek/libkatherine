# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

add_executable(ksim main.c)
target_link_libraries(ksim PRIVATE katherine katherine::private)
target_compile_features(ksim PRIVATE c_std_11)

install(TARGETS ksim RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
