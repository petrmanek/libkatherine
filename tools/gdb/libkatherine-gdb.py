#!/usr/bin/env python3
# GDB pretty-printers for libkatherine's public structs.
#
# Copyright (c) 2018 Petr Manek.
# This software is distributed under the terms of the MIT License, copied
# verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT
#
# Single source of truth: every printer below renders a value by calling
# the very same katherine_<name>_snprint() the C, C++ and Python surfaces
# all delegate to (c/src/repr.c) -- via gdb.parse_and_eval() against the
# live inferior. Nothing here reimplements the formatting. That is also
# this script's one hard requirement: it needs a running, callable process.
# Against a core dump (or any other target where inferior function calls
# are impossible), the lookup function quietly declines instead -- so gdb
# treats the value as if no printer had matched it at all, falling back to
# its own default struct display rather than failing (see
# _lookup_katherine_printer()'s docstring for why it has to be the lookup
# function, and not to_string(), that declines).
#
# Installed as <soname-file>-gdb.py next to the library (see the install()
# rule in c/CMakeLists.txt), per gdb's objfile auto-load convention: gdb
# looks for a script named after the exact path of the shared object it has
# mapped, plus "-gdb.py". For a few reasons unrelated to this script, that
# directory is usually NOT on gdb's auto-load safe-path by default, so the
# script is not sourced automatically until the user opts in, e.g. with one
# of:
#   echo "add-auto-load-safe-path /usr/lib" >> ~/.gdbinit
#   (gdb) add-auto-load-safe-path /usr/lib/x86_64-linux-gnu/libkatherine.so.1.0.1
# or by passing -iex "set auto-load safe-path /" for a one-off session.
# See `help set auto-load safe-path` in gdb for the full story. Absent that,
# `source /path/to/libkatherine-gdb.py` still works at any time.
#
# katherine_px_config_t (embeds a 16384-word matrix) and katherine_config_t
# (embeds one) are themselves larger than gdb's default `max-value-size`
# (65536 bytes): gdb refuses to fetch a value that big at all, for any
# type, before any pretty-printer -- ours included -- ever sees it. Printing
# one of these two needs `set max-value-size unlimited` first.

import gdb

# Generous scratch buffer for the inferior call: every covered type's
# rendering is at most a few hundred bytes (katherine_config_t, the
# largest, nests five other structs and a pixel-matrix digest), so this
# never truncates in practice; katherine_*_snprint()'s own return value is
# still honored below regardless, exactly as a real caller would.
_SCRATCH_CAP = 4096

# tag (the struct/union name gdb reports once typedefs are stripped, i.e.
# the C name without the trailing "_t") -> the katherine_*_snprint()
# function that is the single source of truth for rendering it. Mirrors
# the coverage of c/src/repr.c exactly.
_SNPRINT_BY_TAG = {
    'katherine_coord': 'katherine_coord_snprint',
    'katherine_px_f_toa_tot': 'katherine_px_f_toa_tot_snprint',
    'katherine_px_toa_tot': 'katherine_px_toa_tot_snprint',
    'katherine_px_f_toa_only': 'katherine_px_f_toa_only_snprint',
    'katherine_px_toa_only': 'katherine_px_toa_only_snprint',
    'katherine_px_f_event_itot': 'katherine_px_f_event_itot_snprint',
    'katherine_px_event_itot': 'katherine_px_event_itot_snprint',
    'katherine_trigger': 'katherine_trigger_snprint',
    'katherine_test_pulse_config': 'katherine_test_pulse_config_snprint',
    'katherine_dacs': 'katherine_dacs_snprint',
    'katherine_px_config': 'katherine_px_config_snprint',
    'katherine_config': 'katherine_config_snprint',
    'katherine_frame_info_time': 'katherine_frame_info_time_snprint',
    'katherine_frame_info': 'katherine_frame_info_snprint',
    'katherine_acquisition': 'katherine_acquisition_snprint',
    'katherine_readout_status': 'katherine_readout_status_snprint',
    'katherine_comm_status': 'katherine_comm_status_snprint',
    'katherine_udp': 'katherine_udp_snprint',
    'katherine_device': 'katherine_device_snprint',
}


def _call_snprint(fn_name, addr):
    """Renders *addr by calling fn_name(buf, cap, addr) in the live
    inferior: malloc() a scratch buffer, call the snprint function into it,
    read back its own would-be-length return value's worth of bytes, then
    free() the buffer. Returns the decoded string, or None if any step
    could not be carried out (no running/callable inferior -- e.g. a core
    dump -- or a missing symbol), so the caller falls back to gdb's default
    display.
    """
    try:
        buf = gdb.parse_and_eval('(char *) malloc((size_t) %d)' % _SCRATCH_CAP)
        if int(buf) == 0:
            return None
    except gdb.error:
        return None

    try:
        try:
            call = '(int) %s((char *) %d, (size_t) %d, %s)' % (fn_name, int(buf), _SCRATCH_CAP, addr)
            n = int(gdb.parse_and_eval(call))
        except gdb.error:
            return None

        if n < 0:
            return None

        length = min(n, _SCRATCH_CAP - 1)
        if length <= 0:
            return ''

        try:
            data = gdb.selected_inferior().read_memory(buf, length)
        except (gdb.error, gdb.MemoryError):
            return None

        return bytes(data).decode('utf-8', errors='replace')
    finally:
        try:
            gdb.parse_and_eval('(void) free((void *) %d)' % int(buf))
        except gdb.error:
            pass  # the rendering above already succeeded or failed either way


class _KatherineRenderedPrinter(object):
    """Trivial to_string() wrapper around a rendering already produced by
    _call_snprint(). Splitting it this way (rather than calling snprint
    lazily from to_string()) is what makes the core-dump fallback work: per
    gdb's pretty-printer API, a to_string() that returns None prints
    nothing at all for the value, but a *lookup function* that returns None
    makes gdb treat the value as if no pretty-printer had matched it,
    falling through to its own default struct display -- so the call has
    to be attempted, and found to succeed, before a printer object (this
    one) is even created."""

    def __init__(self, rendered):
        self._rendered = rendered

    def to_string(self):
        return self._rendered


def _lookup_katherine_printer(val):
    t = val.type.strip_typedefs()
    if t.code not in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
        return None

    fn_name = _SNPRINT_BY_TAG.get(t.tag)
    if fn_name is None:
        return None

    addr = val.address
    if addr is None:
        # A value with no address (e.g. optimized into a register) has
        # nothing a pointer-taking snprint function could be called on.
        return None

    rendered = _call_snprint(fn_name, '(const void *) %d' % int(addr))
    if rendered is None:
        return None  # gdb falls back to its own default struct display

    return _KatherineRenderedPrinter(rendered)


gdb.pretty_printers.append(_lookup_katherine_printer)
