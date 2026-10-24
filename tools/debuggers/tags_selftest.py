#!/usr/bin/env python3
"""Self-test for the pretty-printers' list of covered types.

libkatherine_debuggers.TAGS names the types GDB and LLDB can render, and the
rule stated in this directory's README is that adding a katherine_*_snprint()
means adding exactly one tag. Nothing checked it, and the 2.0 configuration
split added two renderers that the list never heard about, leaving a debugger
showing the raw struct for the type callers actually hold.

Run it with no arguments:

    python3 tools/debuggers/tags_selftest.py

It reads the sources and nothing else, so it needs no build and no debugger.

@copyright Copyright (c) 2018 Petr Mánek.
This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".

SPDX-License-Identifier: MIT
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import libkatherine_debuggers as dbg  # noqa: E402

# A definition or a declaration, both of which start the name at column 0 in
# this tree's style. The paren is load-bearing: without it
# katherine_usb_config_snprint_address() would answer to this as well.
SNPRINT = re.compile(r"^(katherine_[a-z0-9_]+)_snprint\(", re.MULTILINE)


def rendered_types():
    """Every type c/src gives a snprint of, by tag."""
    found = set()
    for dirpath, _, filenames in os.walk(os.path.join(ROOT, "c", "src")):
        for name in filenames:
            if not name.endswith((".c", ".h")):
                continue
            with open(os.path.join(dirpath, name), encoding="utf-8") as f:
                found.update(SNPRINT.findall(f.read()))
    return found


def main():
    found = rendered_types()
    if not found:
        print("FAIL no katherine_*_snprint() found at all; is %s a checkout?" % ROOT)
        return 1

    missing = sorted(found - dbg.TAGS)
    extra = sorted(dbg.TAGS - found)
    for tag in missing:
        print("FAIL %s_snprint() exists, but %s is not in TAGS" % (tag, tag))
    for tag in extra:
        print("FAIL TAGS names %s, which has no %s_snprint()" % (tag, tag))

    print("%d renderers, %d not covered, %d covering nothing"
          % (len(found), len(missing), len(extra)))
    return 1 if missing or extra else 0


if __name__ == "__main__":
    sys.exit(main())
