# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

# GDB pretty-printers for the debug stringification facility (c/src/repr.c):
# they call back into the target process's own katherine_*_snprint() rather
# than reimplementing any formatting. This installs the script next to the
# library, renamed to match gdb's objfile auto-load convention: gdb looks
# for a script named after the exact path of the shared object it has
# mapped, plus "-gdb.py". With VERSION/SOVERSION set on the library target,
# that mapped file is the real, fully-versioned one (e.g.
# libkatherine.so.1.1.0) -- CMake's other two symlinks (the SONAME and dev
# links) are never what gets opened -- hence $<TARGET_FILE_NAME:...> here
# rather than the target's plain output name. (The katherine target is
# visible here because tools/ enters the build after c/.)
install(FILES libkatherine-gdb.py
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RENAME "$<TARGET_FILE_NAME:katherine>-gdb.py")

# LLDB has no auto-load convention for ELF objects, so its script keeps a
# stable, documented name and the user imports it explicitly (see the
# script's header). Both debugger scripts share libkatherine_debuggers.py,
# the single list of covered types, which therefore installs beside them.
install(FILES libkatherine_lldb.py libkatherine_debuggers.py
        DESTINATION ${CMAKE_INSTALL_LIBDIR})
