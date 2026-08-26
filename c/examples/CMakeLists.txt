# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

add_executable(kfind kfind.c)
target_link_libraries(kfind PRIVATE katherine)
katherine_target_warnings(kfind)

add_executable(krun krun.c)
target_link_libraries(krun PRIVATE katherine)
# strerror() has no portable replacement that is also safe under concurrent
# access from other threads of the same process; this example is
# single-threaded, so the CRT's objection does not apply.
target_compile_definitions(krun PRIVATE $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
katherine_target_warnings(krun)

add_executable(ktemp ktemp.c)
target_link_libraries(ktemp PRIVATE katherine)
# See the krun target above: same single-threaded strerror() usage.
target_compile_definitions(ktemp PRIVATE $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
katherine_target_warnings(ktemp)

add_executable(kinfo kinfo.c)
target_link_libraries(kinfo PRIVATE katherine)
# See the krun target above: same single-threaded strerror() usage.
target_compile_definitions(kinfo PRIVATE $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
katherine_target_warnings(kinfo)
