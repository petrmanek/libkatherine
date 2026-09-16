# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

#[=======================================================================[
KatherineWarnings
------------------

Pedantic compiler warnings for the Katherine project.

katherine_target_warnings(<target>)

  Applies -Wall -Wextra -Wpedantic (GCC, Clang, AppleClang) or /W4 (MSVC)
  to <target> as a PRIVATE compile option: it shapes the diagnostics of
  this project's own sources without ever reaching a consumer that links
  against <target>, whether the flag is added here to katherine itself or
  by every caller of katherine_add_test() / katherine_add_bench().

  GCC additionally gets -Wimplicit-fallthrough=5, which -Wextra enables
  only at level 3. Level 5 accepts nothing but the attribute, where 3 also
  takes a comment saying so -- and a comment is not checkable, which is the
  whole reason to raise it. GCC-only: Clang rejects the =N spelling and its
  own -Wimplicit-fallthrough is not in -Wextra for C, and MSVC diagnoses
  fallthrough at no warning level, not even under /analyze.
#]=======================================================================]

include_guard(GLOBAL)

function(katherine_target_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall -Wextra -Wpedantic>
        $<$<OR:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>:/W4>
        $<$<OR:$<C_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:GNU>>:-Wimplicit-fallthrough=5>
    )
endfunction()
