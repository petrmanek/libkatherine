# Katherine debugger pretty-printers

Scripts that make GDB and LLDB render libkatherine's public structures the
same way every other surface does: by calling the debugged process's own
`katherine_*_snprint()` functions (`c/src/repr.c`), so a value inspected in
a debugger prints byte-identically to C, C++ (`operator<<`) and Python
(`repr()`). Nothing here reimplements any formatting; the shared
`libkatherine_debuggers.py` holds the one list of covered types, and both
debugger scripts dispatch through it — adding a new `katherine_*_snprint()`
means adding exactly one tag there.

Because the rendering is a real function call into the target, a **running,
stopped process** is required. Against a core dump, GDB falls back to its
default struct display and LLDB simply shows no summary (the expandable
children remain), so nothing breaks — you just get the raw view.

Example, stopped at a breakpoint:

    (gdb) print config.test_pulse_config
    $1 = test_pulse_config{enabled: true, digital_only: false, external: false, count: 100, period: 65, phase: 0}

## GDB

The CMake install step deposits the script next to the shared library,
renamed to `<library file>-gdb.py` per GDB's objfile auto-load convention —
GDB finds it by itself whenever it loads the library. The one-time catch:
that directory is usually not on GDB's *auto-load safe path*, so until you
opt in, GDB will mention the skipped script instead of sourcing it. Opt in
once with, e.g.:

    echo "add-auto-load-safe-path /usr/lib" >> ~/.gdbinit

(or see `help set auto-load safe-path` in GDB for narrower forms). Without
auto-load, `source /usr/lib/libkatherine.so.<version>-gdb.py` works in any
session.

Note that `katherine_config_t` and `katherine_px_config_t` embed a 64 KiB
pixel matrix, which exceeds GDB's default `max-value-size` — GDB refuses to
fetch values that large for any printer. `set max-value-size unlimited`
first when printing those two.

## LLDB

LLDB has no auto-load convention for ELF shared objects, so the script is
registered explicitly — once, in your init file:

    echo 'command script import /usr/lib/libkatherine_lldb.py' >> ~/.lldbinit

after which every session loads it automatically. For a one-off session,
run the same `command script import` at the LLDB prompt. (A project-local
`.lldbinit` also works, but LLDB ignores cwd init files unless you set
`settings set target.load-cwd-lldbinit true` globally — the same
opt-in-once spirit as GDB's safe path.)

Both scripts and the shared module install into the library directory
(`lib/` under the install prefix); adjust the paths above to your prefix.
