# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

add_executable(kfindxx kfindxx.cpp address_source.cpp address_source.hpp)
target_link_libraries(kfindxx PRIVATE katherinexx)
# MSVC only enables C++ exception unwind semantics with /EHsc; without it,
# every throw/catch this example and the standard library it uses (e.g.
# katherine::error in error.hpp, <iostream>) reach is unwind-unsafe and warns.
target_compile_options(kfindxx PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
# katherine::error (error.hpp) formats std::strerror() into its message;
# that has no portable replacement safe under concurrent access from other
# threads of the same process, and every example here is single-threaded.
target_compile_definitions(kfindxx PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
katherine_target_warnings(kfindxx)

add_executable(krunxx krunxx.cpp)
target_link_libraries(krunxx PRIVATE katherinexx)
# The one example written against C++14 (chrono literals). The compile
# feature raises just this target when the build-wide standard is older,
# so a -DCMAKE_CXX_STANDARD=11 build of the C++11 library still succeeds.
target_compile_features(krunxx PRIVATE cxx_std_14)
# See the kfindxx target above: same /EHsc and single-threaded
# katherine::error requirements.
target_compile_options(krunxx PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
target_compile_definitions(krunxx PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
katherine_target_warnings(krunxx)

add_executable(ktempxx ktempxx.cpp)
target_link_libraries(ktempxx PRIVATE katherinexx)
# See the kfindxx target above: same /EHsc and single-threaded
# katherine::error requirements.
target_compile_options(ktempxx PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
target_compile_definitions(ktempxx PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
katherine_target_warnings(ktempxx)

add_executable(kinfoxx kinfoxx.cpp)
target_link_libraries(kinfoxx PRIVATE katherinexx)
# See the kfindxx target above: same /EHsc and single-threaded
# katherine::error requirements.
target_compile_options(kinfoxx PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
target_compile_definitions(kinfoxx PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
katherine_target_warnings(kinfoxx)
