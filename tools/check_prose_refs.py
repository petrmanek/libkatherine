#!/usr/bin/env python3
"""Every katherine_*() a comment names must be something that exists.

Doc blocks in this tree point the reader at other functions constantly --
"see katherine_transport_get_last_os_error()", "asked for by
katherine_device_discover_udp()". A rename moves the function and leaves the
sentence behind, and nothing notices: Doxygen only resolves what is written
as a \\ref, the compiler never reads a comment, and the name still looks
plausible. Four such references had accumulated by 2.0, one of them eight
times in a single header.

Run it with no arguments:

    python3 tools/check_prose_refs.py

Sources only. CHANGELOG.md and docs/migration-1.x.md name 1.x symbols on
purpose, which is the whole point of them, so they are not read here.

@copyright Copyright (c) 2018 Petr Mánek.
This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".

SPDX-License-Identifier: MIT
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TREES = ("c/include", "c/src", "cxx/include", "python")
SUFFIXES = (".c", ".h", ".hpp", ".pxd", ".pyx")

IDENT = re.compile(r"\bkatherine_[A-Za-z0-9_]+\b")
# A reference reads as a call. Anything else -- a type, a field, a macro
# mentioned bare -- is too easy to confuse with ordinary prose.
CALL = re.compile(r"\b(katherine_[A-Za-z0-9_]+)\(\)")
# A C or C++ comment line, or a Cython/Python one. "/*" is here for the
# one-line doc block, `/** ... */`, which the house style uses for a field
# or a \copybrief and which carries 51 references in this tree. A line of
# code may also carry a trailing comment, which this deliberately does not
# try to find: the references worth checking are in doc blocks.
COMMENT = ("*", "/*", "//", "#")


def sources():
    for tree in TREES:
        for dirpath, _, filenames in os.walk(os.path.join(ROOT, tree)):
            for name in filenames:
                if name.endswith(SUFFIXES):
                    yield os.path.join(dirpath, name)


# What to skip when harvesting declarations. COMMENT minus "#", because a
# line opening with # is a Cython comment and a C preprocessor directive
# both, and katherine1.h declares the 1.x aliases as #define. Keeping "/*"
# matters: a name only a one-line doc block spells is not declared, and
# reading it as code here would let it excuse itself there.
NOT_CODE = ("*", "/*", "//")


def declared():
    """Every katherine_* identifier the code itself spells, comments aside."""
    names = set()
    for path in sources():
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.lstrip().startswith(NOT_CODE):
                    continue
                names.update(IDENT.findall(line))
    return names


def main():
    known = declared()
    if not known:
        print("FAIL no katherine_* identifiers found at all; is %s a checkout?" % ROOT)
        return 1

    dangling = []
    seen = 0
    for path in sources():
        rel = os.path.relpath(path, ROOT)
        with open(path, encoding="utf-8", errors="replace") as f:
            for number, line in enumerate(f, 1):
                if not line.lstrip().startswith(COMMENT):
                    continue
                for name in CALL.findall(line):
                    seen += 1
                    if name not in known:
                        dangling.append((rel, number, name))

    for rel, number, name in dangling:
        print("FAIL %s:%d names %s(), which this tree does not define" % (rel, number, name))
    print("%d references in comments, %d dangling" % (seen, len(dangling)))
    return 1 if dangling else 0


if __name__ == "__main__":
    sys.exit(main())
