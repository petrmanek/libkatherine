#!/usr/bin/env python3
# LLDB type summaries for libkatherine's public structs.
#
# Copyright (c) 2018 Petr Manek.
# This software is distributed under the terms of the MIT License, copied
# verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT
#
# Single source of truth: every summary renders a value by calling the very
# same katherine_<name>_snprint() the C, C++ and Python surfaces all
# delegate to (c/src/repr.c), evaluated in the live process. Nothing here
# reimplements the formatting; the set of covered types lives in
# libkatherine_debuggers.py next to this script. Like its gdb sibling, this
# needs a running, callable process: where inferior calls are impossible
# (core dumps), the summary quietly stays empty and lldb's expandable
# children remain the way to inspect the value.
#
# LLDB has no auto-load convention for ELF shared objects, so this script
# is loaded explicitly -- once per session or from ~/.lldbinit:
#   (lldb) command script import /usr/lib/libkatherine_lldb.py
# The filename uses underscores because lldb imports it under its module
# name, which the summary registrations below reference.

import lldb

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    import libkatherine_debuggers as _kat
finally:
    sys.path.pop(0)


def _render(target, fn_name, addr):
    """Renders the struct at addr by calling fn_name(buf, cap, addr) in the
    live process: malloc() a scratch buffer, call the snprint function into
    it, read back its would-be-length return value's worth of bytes, then
    free() the buffer. Returns the decoded string, or None if any step
    could not be carried out. Casts use gdb/lldb built-in types
    (unsigned long, not size_t): a typedef is only evaluable when the
    debuggee's own debug info happens to carry it."""
    buf = target.EvaluateExpression('(char *) malloc((unsigned long) %d)' % _kat.SCRATCH_CAP)
    if not buf.IsValid() or buf.GetValueAsUnsigned() == 0:
        return None
    buf_addr = buf.GetValueAsUnsigned()

    try:
        call = '(int) %s((char *) %dUL, (unsigned long) %d, (const void *) %dUL)' \
            % (fn_name, buf_addr, _kat.SCRATCH_CAP, addr)
        n_val = target.EvaluateExpression(call)
        if not n_val.IsValid() or n_val.GetError().Fail():
            return None
        n = n_val.GetValueAsSigned()
        if n < 0:
            return None

        length = min(n, _kat.SCRATCH_CAP - 1)
        if length <= 0:
            return ''

        err = lldb.SBError()
        data = target.GetProcess().ReadMemory(buf_addr, length, err)
        if err.Fail() or data is None:
            return None
        return data.decode('utf-8', errors='replace')
    finally:
        target.EvaluateExpression('(void) free((void *) %dUL)' % buf_addr)


def katherine_summary(valobj, internal_dict):
    """Summary callback for every covered type (registered via one regex
    below): dispatches on the value's own type name through the shared
    table. Returning '' on failure leaves lldb's expandable children as the
    fallback view -- unlike gdb, a summary and the default child display
    coexist, so nothing is lost."""
    tag = _kat.tag_of(valobj.GetType().GetUnqualifiedType().GetCanonicalType().GetName() or '')
    if tag is None:
        return ''

    process = valobj.GetProcess()
    if not process.IsValid() or process.GetState() != lldb.eStateStopped:
        return ''

    addr = valobj.GetLoadAddress()
    if addr == lldb.LLDB_INVALID_ADDRESS:
        return ''

    rendered = _render(valobj.GetTarget(), _kat.snprint_name(tag), addr)
    return rendered if rendered is not None else ''


def __lldb_init_module(debugger, internal_dict):
    # One regex summary covering every tag, with and without the struct
    # keyword and the _t typedef suffix; the callback re-checks against the
    # shared table, so an over-broad regex match renders nothing.
    pattern = r'^(struct |union )?(%s)(_t)?$' % '|'.join(sorted(_kat.TAGS))
    debugger.HandleCommand(
        'type summary add --regex "%s" --python-function %s.katherine_summary --category katherine'
        % (pattern, __name__))
    debugger.HandleCommand('type category enable katherine')
