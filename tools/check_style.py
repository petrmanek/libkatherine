#!/usr/bin/env python3
"""House-style checks the compiler cannot make.

Three families, each corrected by hand in review more than once, none of
them a compile error:

1. A function that uses `goto err_*` for one failure and a bare `return`
   for another is half-converted: the cleanup it jumps to is skipped by the
   early return. Every exit but the single success goes through a label.
2. A native integer type where a fixed-width one is meant, and a parameter
   list wrapped over two lines, which this codebase does not do.
3. A doc block or field comment that asks rather than names -- "What the
   readout is" -- that ends on a verb, or that leaves a bool's polarity
   unsaid.

Run it with no arguments and it reads SCOPE below; give it paths and it
reads those instead, which is how the review harness uses it.

Scope is c/src and nothing else, deliberately. The rest of the tree is not
at zero -- at the time of writing c/include has 54 findings, c/tests 128,
cxx 2 and tools 3 -- and a check that fails on arrival teaches everyone to
ignore it. Widening it is a decision with a number attached, not an
oversight.

@copyright Copyright (c) 2018 Petr Mánek.
This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".

SPDX-License-Identifier: MIT
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCOPE = ("c/src",)
SUFFIXES = (".c", ".h")


def sources():
    for tree in SCOPE:
        for dirpath, _, filenames in os.walk(os.path.join(ROOT, tree)):
            for name in sorted(filenames):
                if name.endswith(SUFFIXES):
                    yield os.path.join(dirpath, name)

FUNC = re.compile(r'^(?!static|typedef|#)([a-z_][a-z0-9_]*\s*\()', re.M)


def functions(src):
    """Yield (name, body, line) for each top-level function definition."""
    for m in re.finditer(r'^([a-z_][a-z0-9_]*)\([^;]*?\)\s*\n\{', src, re.M):
        start = m.end() - 1
        depth, i = 0, start
        while i < len(src):
            if src[i] == '{':
                depth += 1
            elif src[i] == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        yield m.group(1), src[start:i], src[:m.start()].count('\n') + 1


def strip_comments(src):
    """Blank out comments, keeping line numbers, so prose cannot be read as code."""
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith('/*', i):
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(c if c == '\n' else ' ' for c in src[i:j]))
            i = j
        elif src.startswith('//', i):
            j = src.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        else:
            out.append(src[i])
            i += 1
    return ''.join(out)


def check(path):
    src = strip_comments(open(path, encoding='utf-8').read())
    bad = []

    for name, body, line in functions(src):
        if 'goto err' not in body:
            continue
        # Everything from the first error label on is the cascade itself, and
        # its single `return res;` is the idiom rather than a breach of it.
        first = re.search(r'^err[a-z0-9_]*:', body, re.M)
        head = body[:first.start()] if first else body
        for m in re.finditer(r'\breturn\s+([^;]+);', head):
            what = m.group(1).strip()
            if what == 'KATHERINE_E_OK':
                continue
            at = line + head[:m.start()].count('\n')
            bad.append('%s:%d: %s() mixes `goto err_*` with `return %s;`'
                       % (path, at, name, what))

    # Native integer types, including the spellings the first version of this
    # check waved through -- `unsigned char chip_index` reached review because
    # `char` was on that exemption list.
    for m in re.finditer(r'\b(unsigned|signed)\s+(char|short|int|long(\s+long)?)\b', src):
        # A cast in a printf argument is the portable spelling of %lu and %llu,
        # not a type choice: `(unsigned long long) v->timestamp` is correct and
        # flagging it would teach everyone to ignore this check.
        if src[max(0, m.start() - 1):m.start()] == '(':
            continue
        at = src[:m.start()].count('\n') + 1
        bad.append('%s:%d: `%s`; prefer a fixed-width type'
                   % (path, at, ' '.join(m.group(0).split())))
    for m in re.finditer(r'^\s*(?:const\s+)?unsigned\s+(?!char|short|long|int\b)[a-z_]', src, re.M):
        at = src[:m.start()].count('\n') + 1
        bad.append('%s:%d: bare `unsigned`; prefer a fixed-width type' % (path, at))

    for m in re.finditer(r'^(katherine_?[a-z0-9_]*|[a-z_][a-z0-9_]*)\([^)]*,\s*$', src, re.M):
        at = src[:m.start()].count('\n') + 1
        bad.append('%s:%d: wrapped signature; this codebase keeps the parameter '
                   'list on one line whatever its length' % (path, at))

    bad += check_docs(path)
    return bad


# Doc comments that name a thing: a type's block and a field's trailing one.
# Function documentation is excluded by the presence of \param or \retval,
# because a function's brief is written as an imperative and may end on a verb
# where a noun phrase may not.
DOC_BLOCK = re.compile(r'/\*\*(.*?)\*/', re.S)
DOC_FIELD = re.compile(r'///<(.*)')
INTERROGATIVE = ('what', 'how', 'why', 'when', 'which', 'where', 'whether')

# A bool field states what `true` means, not merely what the flag is about:
# "True to <effect>" where the caller sets it, "True if <condition>" where the
# device reports it. "Whether ..." names the subject and leaves the polarity
# unsaid, which is the edit Petr made on 2026-10-17.
BOOL_OPENERS = ('true to', 'true if', 'true when')

# Seeded from what has actually been flagged in review, and extended as more
# turn up. A trailing -ing is treated as a participle on sight.
TRAILING_VERBS = {
    'drives', 'holds', 'carries', 'uses', 'sets', 'reports', 'fires', 'writes',
    'reads', 'sends', 'returns', 'produces', 'determines', 'applies', 'selects',
    'accepts', 'expects', 'contains', 'provides', 'means', 'does', 'runs',
    'starts', 'stops', 'enables', 'disables', 'changes', 'moves', 'counts',
    'measures', 'timestamps', 'is', 'are', 'was', 'were', 'has', 'have',
}


def _first_sentence(text):
    flat = ' '.join(text.replace('*', ' ').split())
    return flat.split('.')[0].strip()


def bool_fields(src, path):
    """A bool field whose comment does not say what `true` means."""
    bad = []
    for m in re.finditer(r'^\s*bool\s+[a-z_][a-z0-9_]*\s*;\s*///<\s*(.+)$', src, re.M):
        text = m.group(1).strip()
        # "Internal:" marks a field the caller never touches, 27 of them in the
        # public headers. The polarity rule is for the documented surface, so
        # the prefix exempts rather than merely shifts the check.
        if text.lower().startswith('internal:'):
            continue
        if not text.lower().startswith(BOOL_OPENERS):
            at = src[:m.start()].count('\n') + 1
            bad.append('%s:%d: bool comment opens "%s"; say what true means '
                       '("True to ..." to set, "True if ..." to report)'
                       % (path, at, text.split()[0]))
    return bad


def param_openers(src, path):
    r"""A \param whose description asks rather than names.

    The block-level rules below skip any doc carrying \param, because a
    function brief is an imperative. That exemption used to cover the \param
    lines inside it too, which is how "How long to collect answers for" and
    "Where the answers are written" reached review on 2026-10-17. The
    parameter descriptions are noun phrases and are checked here.
    """
    bad = []
    for m in re.finditer(r'\\param\s+(?:\[[a-z,]+\]\s*)?([A-Za-z_][A-Za-z0-9_]*)\s+(.+)', src):
        text = m.group(2).strip()
        words = text.split()
        if not words:
            continue
        if words[0].lower() in INTERROGATIVE:
            at = src[:m.start()].count('\n') + 1
            bad.append('%s:%d: \\param %s opens with "%s"; name what it is, '
                       'do not ask about it' % (path, at, m.group(1), words[0]))
    return bad


def check_docs(path):
    """Interrogative openers and trailing verbs in type and field comments."""
    src = open(path, encoding='utf-8').read()
    bad = bool_fields(src, path) + param_openers(src, path)
    spans = [(m.start(1), m.group(1)) for m in DOC_BLOCK.finditer(src)]
    spans += [(m.start(1), m.group(1)) for m in DOC_FIELD.finditer(src)]

    for off, text in spans:
        if '\\param' in text or '\\retval' in text or '\\file' in text:
            continue
        s = _first_sentence(text)
        if not s:
            continue
        at = src[:off].count('\n') + 1
        words = s.split()
        if words[0].lower().strip('\\') in INTERROGATIVE:
            bad.append('%s:%d: doc opens with "%s"; name the thing, do not ask about it'
                       % (path, at, words[0]))
        last = words[-1].lower().strip(',;:')
        if last in TRAILING_VERBS or (last.endswith('ing') and len(last) > 5):
            bad.append('%s:%d: doc sentence ends on the verb "%s"; make it a noun phrase'
                       % (path, at, last))
    return bad



def main(argv):
    paths = argv[1:] or sorted(sources())
    found = []
    for p in paths:
        found += check(p)
    for f in found:
        print(f)
    print("style: %d finding(s) over %d file(s)" % (len(found), len(paths)))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
