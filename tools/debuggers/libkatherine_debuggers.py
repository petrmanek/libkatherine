#!/usr/bin/env python3
# Debugger-agnostic support for libkatherine's pretty-printers.
#
# Copyright (c) 2018 Petr Manek.
# This software is distributed under the terms of the MIT License, copied
# verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT
#
# The single place that knows WHAT can be rendered: the debugger-specific
# scripts next to this module (libkatherine-gdb.py, libkatherine_lldb.py)
# only know HOW to call into their inferior. Adding a new
# katherine_<name>_snprint() to c/src/repr.c therefore means adding exactly
# one tag below, and every debugger picks it up.

# Struct/union tags covered by c/src/repr.c, i.e. the C type names without
# the trailing "_t". The rendering function for a tag is always
# "<tag>_snprint" (see snprint_name()).
TAGS = frozenset((
    'katherine_coord',
    'katherine_px_f_toa_tot',
    'katherine_px_toa_tot',
    'katherine_px_f_toa_only',
    'katherine_px_toa_only',
    'katherine_px_f_event_itot',
    'katherine_px_event_itot',
    'katherine_trigger',
    'katherine_test_pulse_config',
    'katherine_dacs',
    'katherine_px_config',
    'katherine_config',
    'katherine_frame_info_time',
    'katherine_frame_info',
    'katherine_acquisition',
    'katherine_readout_status',
    'katherine_comm_status',
    'katherine_udp',
    'katherine_device',
))

# Generous scratch buffer for the inferior call: every covered type's
# rendering is at most a few hundred bytes (katherine_config_t, the
# largest, nests five other structs and a pixel-matrix digest), so this
# never truncates in practice; katherine_*_snprint()'s own return value is
# still honored by the callers regardless, exactly as a real caller would.
SCRATCH_CAP = 4096


def tag_of(type_name):
    """Maps a debugger-reported type name to a covered tag, or None: strips
    an optional 'struct '/'union ' prefix and an optional '_t' typedef
    suffix, then requires an exact match against TAGS."""
    name = type_name
    for prefix in ('struct ', 'union ', 'const '):
        if name.startswith(prefix):
            name = name[len(prefix):]
    if name.endswith('_t'):
        name = name[:-2]
    return name if name in TAGS else None


def snprint_name(tag):
    """The katherine_*_snprint() function rendering the given tag."""
    return tag + '_snprint'
