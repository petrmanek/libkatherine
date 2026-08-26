# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

add_executable(ksim main.c)
target_link_libraries(ksim PRIVATE katherine katherine::private)
target_compile_features(ksim PRIVATE c_std_11)
katherine_target_warnings(ksim)
# ksim's strerror()/fopen() calls have no portable replacement that is also
# safe under concurrent access from other threads of the same process or
# distrusts a caller-controlled path; ksim is single-threaded and its
# --log path is a trusted operator argument, so the CRT's objection does
# not apply.
target_compile_definitions(ksim PRIVATE $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

install(TARGETS ksim RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
