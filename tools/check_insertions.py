#!/usr/bin/env python3
"""Flag diff hunks that insert between a comment and the code it documents.

Anchoring an edit on a function's signature lands the new lines between that
function and its doc block, which then documents the insertion instead. The
mistake is easy to make, invisible to the compiler, and has happened here
repeatedly: a Doxygen block orphaned from katherine_device_fini(), the same in
acquisition.c, a line in krun.c that stole `// Test pulses are disabled unless
requested above.` from config->test_pulse_config.

The signature is: the context line immediately BEFORE an added block is a
comment (or a comment terminator), and the context line immediately AFTER it is
code. That means the comment now introduces the new code and no longer
introduces what it was written for. Hunks that also remove adjacent lines are
ignored, since those lines were replaced rather than inserted into.

Usage:  python3 tools/check_insertions.py [--cached]
Exit:   0 clean, 1 suspicious hunks found.
"""

import re
import subprocess
import sys

COMMENT_END = re.compile(r"^\s*(\*/|//|\*|/\*|#)")
CODE = re.compile(r"^\s*\S")
BLANK = re.compile(r"^\s*$")


def looks_like_comment(line: str) -> bool:
    return bool(COMMENT_END.match(line))


def looks_like_code(line: str) -> bool:
    if BLANK.match(line):
        return False
    if looks_like_comment(line):
        return False
    return bool(CODE.match(line))


def main() -> int:
    args = ["git", "diff", "-U3"]
    if "--cached" in sys.argv:
        args.insert(2, "--cached")
    diff = subprocess.run(args, capture_output=True, text=True, check=True).stdout

    path = None
    findings = []
    # Walk the diff keeping the last context line before each run of additions.
    prev_context = None
    in_addition = False
    addition_start_context = None
    saw_removal = False

    for raw in diff.splitlines():
        if raw.startswith("+++ b/"):
            path = raw[6:]
            prev_context = None
            in_addition = False
            continue
        if raw.startswith("@@"):
            prev_context = None
            in_addition = False
            continue
        if not path or raw.startswith(("---", "diff ", "index ", "new file", "deleted")):
            continue

        kind, body = raw[:1], raw[1:]

        if kind == "-":
            # A removal adjacent to the additions means the lines were
            # REPLACED, not inserted -- changing a function's return type, say.
            # Nothing moved between a comment and its subject, so this is not
            # the mistake being looked for.
            saw_removal = True
            if in_addition:
                in_addition = False
            prev_context = None
            continue

        if kind == "+":
            if not in_addition:
                in_addition = True
                addition_start_context = prev_context
                saw_removal = False
            continue

        if kind == " ":
            if in_addition:
                # The addition just ended; `body` is the line following it.
                before = addition_start_context
                after = body
                if (
                    before is not None
                    and not saw_removal
                    and looks_like_comment(before)
                    and looks_like_code(after)
                ):
                    findings.append((path, before.rstrip(), after.rstrip()))
                in_addition = False
            prev_context = body

    if not findings:
        print("check-insertions: clean")
        return 0

    print("check-insertions: %d suspicious insertion(s)\n" % len(findings))
    for path, before, after in findings:
        print("  %s" % path)
        print("    comment before insertion : %s" % before)
        print("    code after insertion     : %s" % after)
        print("    -> the comment now introduces the inserted code, not this line.\n")
    return 1


if __name__ == "__main__":
    sys.exit(main())
