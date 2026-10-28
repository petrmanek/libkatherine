#!/usr/bin/env python3
"""A public symbol that is one ASIC's carries that ASIC's prefix.

WP19's criterion is a judgement, not a pattern: prefix a symbol when a
Timepix2 or Timepix4 equivalent would need a different type or different
semantics, leave it bare when the other ASICs would use it unchanged. No
regular expression decides that, which is why the audit was a reading and
why its result evaporates the moment someone adds a header.

So this does not re-derive the judgement. It freezes it. Every bare public
symbol the audit left bare is recorded in a baseline, and a bare symbol that
is not in that baseline fails the check -- not because it is wrong, but
because nobody has said it is right. The answer is either a prefix or a line
in the baseline, and the second one costs a sentence in a commit message.

That matters only inside one window. 2.0 is the last release that may rename
a public symbol, so a Timepix3-specific name that ships bare is a name the
library carries until 3.0.

Run it with no arguments; pass --update to adopt the current surface after
deciding, deliberately, that a new bare symbol belongs bare.

    python3 tools/check_asic_prefix.py
    python3 tools/check_asic_prefix.py --update

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
HEADERS = os.path.join("c", "include", "katherine")
BASELINE = os.path.join(ROOT, "tools", "asic_prefix_baseline.txt")

# The 1.x compatibility shim is spelled the way 1.x spelled it, which is the
# whole point of it, so its names are not this check's business.
SKIP = ("katherine1.h",)

PREFIXES = ("katherine_tpx3_", "katherine_tpx2_", "katherine_tpx4_", "KATHERINE_TPX3_", "KATHERINE_TPX2_", "KATHERINE_TPX4_")

EXPORTED = re.compile(r"\bKATHERINE_EXPORTED\b")
FUNCTION = re.compile(r"^\s*(katherine_\w+)\s*\(")
TYPEDEF = re.compile(r"^\s*\}\s*(katherine_\w+_t)\s*;")
MACRO = re.compile(r"^\s*#\s*define\s+(KATHERINE_[A-Z0-9_]+)")
ENUMERATOR = re.compile(r"^\s*(KATHERINE_[A-Z0-9_]+)\s*(?:=|,|$)")


def headers():
    """Every public header in the repository, sorted so a report is stable.

    The .in templates are left out: they are configured into the build tree
    and the names they carry are the library's own version and features."""
    directory = os.path.join(ROOT, HEADERS)
    return [os.path.join(directory, n) for n in sorted(os.listdir(directory)) if n.endswith(".h") and n not in SKIP]


def symbols():
    """Every symbol the public headers export, with where it was found.

    A function counts when the line above it carries KATHERINE_EXPORTED,
    which is how this library marks its entry points; a type counts at the
    closing brace that names it; macros and enumerators count where they are
    written. An enumerator is public whether or not the enum is exported,
    because it is a name in the caller's namespace either way."""
    found = {}
    for path in headers():
        rel = os.path.relpath(path, ROOT)
        lines = open(path, encoding="utf-8").read().split("\n")
        for number, line in enumerate(lines):
            name = None
            if number and EXPORTED.search(lines[number - 1]):
                match = FUNCTION.match(line)
                if match:
                    name = match.group(1)
            for pattern in (TYPEDEF, MACRO, ENUMERATOR):
                if name:
                    break
                match = pattern.match(line)
                if match:
                    name = match.group(1)
            if name and name not in found:
                found[name] = (rel, number + 1)
    return found


def baseline():
    """The bare symbols WP19 looked at and left bare."""
    if not os.path.exists(BASELINE):
        return set()
    with open(BASELINE, encoding="utf-8") as f:
        return {line.strip() for line in f if line.strip() and not line.startswith("#")}


def write_baseline(bare):
    with open(BASELINE, "w", encoding="utf-8") as f:
        f.write("# The public symbols that carry no ASIC prefix today.\n")
        f.write("# Written by tools/check_asic_prefix.py --update. A name here has been\n")
        f.write("# seen, which is not the same as having been judged: the file was seeded\n")
        f.write("# from the tree, and what it buys is that the NEXT bare name is judged.\n")
        for name in sorted(bare):
            f.write(name + "\n")


def main():
    update = "--update" in sys.argv[1:]

    found = symbols()
    bare = {name: where for name, where in found.items() if not name.startswith(PREFIXES)}

    if update:
        write_baseline(set(bare))
        print("%d bare symbol(s) recorded" % len(bare))
        return 0

    recorded = baseline()
    undecided = sorted(name for name in bare if name not in recorded)
    stale = sorted(name for name in recorded if name not in bare)

    for name in undecided:
        rel, number = bare[name]
        print("FAIL %s:%d %s is public and bare" % (rel, number, name))
        print("       either it is Timepix3's, and takes the prefix,")
        print("       or it is the readout's, and goes in the baseline with a reason in the message")
    for name in stale:
        print("STALE %s is in the baseline and no longer in the headers" % name)

    print("%d public symbol(s), %d prefixed, %d bare, %d undecided, %d stale"
          % (len(found), len(found) - len(bare), len(bare), len(undecided), len(stale)))
    return 1 if undecided or stale else 0


if __name__ == "__main__":
    sys.exit(main())
