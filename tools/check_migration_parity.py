#!/usr/bin/env python3
"""A name the shim keeps is a name the migration guide explains.

Two shims carry 1.x spellings into 2.0: katherine/katherine1.h for C and
the _RENAMED_IN_2_0 table in python/katherine.pyx. docs/migration-1.x.md
is what a reader consults instead of either. Nothing has ever compared
them, and the comparison has a direction that matters more than it looks.

check_prose_refs.py sweeps prose for names the code does not have. This
sweeps the code for names the prose does not have -- the omitted
direction, which no tool had and which is where the gap turned out to be:
four C types whose 1.x spelling the guide's own rule maps to the wrong
answer, because event_itot became event_count_itot and not merely
tpx3_event_itot.

So:

  1. Every rename in katherine1.h that inserting `tpx3_` does NOT produce
     must be named in the guide. The ones it does produce are covered by
     the guide's rule and its wildcard rows, and listing a hundred of them
     would bury the eight that matter.
  2. Every key of _RENAMED_IN_2_0 must be named in the guide.
  3. Every `katherine.X` in the left column of the guide's Python table
     must be a key of _RENAMED_IN_2_0 -- otherwise the guide promises a
     deprecation path that no longer exists.

Run it with no arguments:

    python3 tools/check_migration_parity.py

@author Petr Mánek
@date 28.10.26

@copyright Copyright (c) 2018 Petr Mánek.
This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".

SPDX-License-Identifier: MIT
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHIM = os.path.join("c", "include", "katherine", "katherine1.h")
PYX = os.path.join("python", "katherine.pyx")
GUIDE = os.path.join("docs", "migration-1.x.md")

# An object-like #define or a typedef is a rename; a function-like one
# wrapping katherine1_<same stem> is there for the return type and keeps
# its spelling, so it is not one.
OBJECT_DEFINE = re.compile(r"^\s*#\s*define\s+(katherine_\w+|KATHERINE_\w+)\s+(\S+)\s*$", re.M)
TYPEDEF = re.compile(r"^\s*typedef\s+(\w+)\s+(katherine_\w+_t)\s*;", re.M)
# A value of None is a 1.x name with no 2.0 spelling, which the guide owes
# the reader just as much as a rename.
PY_RENAME = re.compile(r"^\s*'(\w+)':\s*(?:'(\w+)'|None),", re.M)
PY_TABLE_ROW = re.compile(r"^\|\s*`katherine\.(\w+)`\s*\|\s*`katherine\.(\w+)`\s*\|", re.M)


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read()


def shim_renames(text):
    """The 1.x C spellings that map to a different 2.0 name."""
    found = {}
    for old, new in OBJECT_DEFINE.findall(text):
        if new.startswith("katherine1_") or "(" in new:
            continue
        found[old] = new
    for new, old in TYPEDEF.findall(text):
        found[old] = new
    return found


def derivable(old, new):
    """Whether inserting the ASIC stem into `old` produces `new`."""
    for stem, asic in (("katherine_", "tpx3_"), ("KATHERINE_", "TPX3_")):
        if old.startswith(stem) and new == stem + asic + old[len(stem):]:
            return True
    return False


def main():
    shim = read(SHIM)
    pyx = read(PYX)
    guide = read(GUIDE)

    findings = []

    renames = shim_renames(shim)
    for old, new in sorted(renames.items()):
        if derivable(old, new) or old in guide:
            continue
        findings.append("%s: %s becomes %s, which the guide's `tpx3_` rule does not produce,\n"
                        "       and the guide does not name it" % (SHIM, old, new))

    python = {old: new or None for old, new in PY_RENAME.findall(pyx)}
    for old in sorted(python):
        if old not in guide:
            findings.append("%s: _RENAMED_IN_2_0 carries %s and the guide does not name it" % (PYX, old))

    for old, new in sorted(PY_TABLE_ROW.findall(guide)):
        if old not in python:
            findings.append("%s: the table maps katherine.%s to katherine.%s, and\n"
                            "       _RENAMED_IN_2_0 has no %s to resolve" % (GUIDE, old, new, old))

    for line in findings:
        print("FAIL " + line)
    print("%d C rename(s), %d Python rename(s) checked against the guide, %d unexplained"
          % (len(renames), len(python), len(findings)))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
