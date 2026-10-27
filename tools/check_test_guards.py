#!/usr/bin/env python3
"""A test case must run under the same preprocessor conditions it compiles
under.

KT_RUN() is what puts a case in the suite, and it is easy to put one under
the nearest #ifdef rather than the right one. Nothing fails when that
happens: the case still compiles, the suite still passes, and the only
sign is an unused-function warning in whichever build configuration drops
it -- which nobody reads, because the suite is green.

It has happened twice in two days. test_port_busy was registered inside
if(KATHERINE_BUILD_EMULATOR) though it needs no emulator; the transport's
never-opened guards were registered inside #ifdef KATHERINE_FEATURE_USB
though the guards are the transport's own. In each case the case simply
did not run in the configuration that dropped it.

So: for every KT_RUN(name), compare the preprocessor conditions in force
at the registration against those in force where the function is defined.
They must match. A case defined under a guard and registered under the
same one is right; a case defined unconditionally and registered under a
guard is this defect; and the reverse does not compile, so it cannot
reach here.

Run it with no arguments:

    python3 tools/check_test_guards.py

@author Petr Mánek
@date 27.10.26

@copyright Copyright (c) 2018 Petr Mánek.
This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".

SPDX-License-Identifier: MIT
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCOPE = (os.path.join("c", "tests"), os.path.join("cxx", "tests"))
SUFFIX = (".c", ".cpp")

OPEN = re.compile(r"#\s*(?:if|ifdef|ifndef)\b")
ELIF = re.compile(r"#\s*elif\b")
ELSE = re.compile(r"#\s*else\b")
CLOSE = re.compile(r"#\s*endif\b")

RUN = re.compile(r"KT_RUN\((\w+)\)")
DEFN = re.compile(r"^(\w+)\(void\)\s*$")
RETURN_TYPE = re.compile(r"^(?:static\s+)?void\s*$")


def sources():
    """Every test source in scope, sorted so the report is stable."""
    found = []
    for scope in SCOPE:
        directory = os.path.join(ROOT, scope)
        if not os.path.isdir(directory):
            continue
        for name in sorted(os.listdir(directory)):
            if name.endswith(SUFFIX):
                found.append(os.path.join(directory, name))
    return found


def guards(lines):
    """The preprocessor conditions in force at each line, as a tuple.

    An #else negates its condition rather than dropping it, so the two arms
    of one conditional do not compare equal -- a case defined in one arm and
    registered in the other is the same defect as one registered under a
    guard it was not defined under."""
    stack = []
    per_line = []
    for line in lines:
        text = line.strip()
        if OPEN.match(text):
            stack.append(text)
        elif ELIF.match(text) and stack:
            stack[-1] = text
        elif ELSE.match(text) and stack:
            stack[-1] = "!" + stack[-1]
        per_line.append(tuple(stack))
        if CLOSE.match(text) and stack:
            stack.pop()
    return per_line


def definitions(lines, per_line):
    """Where each no-argument test function is defined, and under what."""
    found = {}
    for number, line in enumerate(lines):
        match = DEFN.match(line)
        if match and number and RETURN_TYPE.match(lines[number - 1].strip()):
            found[match.group(1)] = per_line[number]
    return found


def main():
    findings = []
    seen = 0
    for path in sources():
        rel = os.path.relpath(path, ROOT)
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.read().split("\n")

        per_line = guards(lines)
        defined = definitions(lines, per_line)

        for number, line in enumerate(lines):
            match = RUN.search(line)
            if not match:
                continue
            name = match.group(1)
            # A case whose definition is not in this file is a macro-built
            # one, which this check has nothing to compare.
            if name not in defined:
                continue
            seen += 1
            if per_line[number] != defined[name]:
                findings.append((rel, number + 1, name, defined[name], per_line[number]))

    for rel, number, name, where_defined, where_run in findings:
        print("FAIL %s:%d registers %s" % (rel, number, name))
        print("       defined under:    %s" % (" ".join(where_defined) or "nothing"))
        print("       registered under: %s" % (" ".join(where_run) or "nothing"))
    print("%d registration(s) checked, %d mismatched" % (seen, len(findings)))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
