#!/usr/bin/env python3
"""Self-test for katherine_errdoc.py.

The indexer decides which error codes a function can return, and the whole
point of it is that a human does not have to. Nothing checked that it was
right. These cases are small C translation units with a known answer, parsed
the way a real source file is, plus the pure-text checks that need no clang
at all.

Run it with no arguments:

    python3 tools/katherine_errdoc_selftest.py

It writes its fixtures to a temporary directory and removes them, so it
claims no global resource and two checkouts cannot collide.

@copyright Copyright (c) 2018 Petr Mánek.
This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".

SPDX-License-Identifier: MIT
"""

import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

try:
    import katherine_errdoc as ed  # noqa: E402
except SystemExit as exc:
    # The tool exits when clang.cindex is absent, which is not a failure of
    # anything this checks. 77 is the code ctest is told to read as a skip.
    print("SKIP: %s" % exc)
    sys.exit(77)

FAILED = []
RAN = 0


def check(label, got, want):
    global RAN
    RAN += 1
    if got != want:
        FAILED.append("%s\n    got:  %r\n    want: %r" % (label, got, want))


def check_true(label, got):
    check(label, bool(got), True)


# ----------------------------------------------------------------- text ---
#
# These need no translation unit, and they guard the two regexes that decide
# whether a function is in scope at all. A mistake here silently drops
# functions from the report rather than reporting something wrong, which is
# the harder kind to notice.

def test_err_marker():
    m = ed.ERR_MARKER
    check_true("a plain return-code marker is recognized",
               m.search(r" * \return Error code."))
    # Three doc blocks continue the sentence instead of ending it, and
    # requiring the full stop left those functions unchecked.
    check_true("a marker continued with a comma is recognized",
               m.search(r" * \return Error code, or KATHERINE_E_OK."))
    check_true("a marker continued with a semicolon is recognized",
               m.search(r" * \return Error code; see below."))
    # An errno is not one of these codes, and the anchoring is what keeps
    # that function out of the report.
    check("an OS error code is not a katherine error code",
          bool(m.search(r" * \return The raw OS error code.")), False)


def test_retval_extraction():
    doc = r"""
     * \retval KATHERINE_E_OK on success.
     * \retval KATHERINE_E_TIMEOUT if the readout did not answer.
     * \return Error code.
    """
    check("every documented code is extracted",
          set(ed.RETVAL.findall(doc)),
          {"KATHERINE_E_OK", "KATHERINE_E_TIMEOUT"})


# ------------------------------------------------------------- fixtures ---

PRELUDE = """
typedef enum katherine_error {
    KATHERINE_E_OK = 0,
    KATHERINE_E_TIMEOUT,
    KATHERINE_E_IO,
    KATHERINE_E_INVAL,
    KATHERINE_E_NOMEM,
    KATHERINE_E_STATE,
} katherine_error_t;
"""

DIRECT_AND_CHAIN = PRELUDE + """
/**
 * The leaf, which is where the code actually comes from.
 * \\retval KATHERINE_E_TIMEOUT if it timed out.
 */
katherine_error_t
leaf(int x)
{
    if (x) return KATHERINE_E_TIMEOUT;
    return KATHERINE_E_OK;
}

/**
 * One hop above the leaf.
 * \\return Error code.
 */
katherine_error_t
middle(int x)
{
    return leaf(x);
}

/**
 * Two hops above the leaf, and contributing a code of its own.
 * \\return Error code.
 */
katherine_error_t
top(int x)
{
    if (x < 0) return KATHERINE_E_INVAL;
    return middle(x);
}
"""

VTABLE = PRELUDE + """
typedef struct link_ops {
    katherine_error_t (*send)(void *ctx);
} link_ops_t;

typedef struct link {
    const link_ops_t *ops;
    void *ctx;
} link_t;

static katherine_error_t
udp_send(void *ctx)
{
    (void) ctx;
    return KATHERINE_E_IO;
}

static const link_ops_t udp_ops = {
    .send = udp_send,
};

/**
 * Reaches udp_send() only through the table, which is what libclang cannot
 * follow on its own.
 * \\return Error code.
 */
katherine_error_t
link_send(link_t *link)
{
    return link->ops->send(link->ctx);
}

const link_ops_t *
anchor(void)
{
    return &udp_ops;
}
"""

# The scanner finds a table by its typedef ending in _ops_t. That is a real
# constraint on how the library may name things, and nothing else states it,
# so it is asserted here: renaming the suffix would disable the expansion
# silently and every wrapper would go back to looking infallible.
VTABLE_WRONG_SUFFIX = VTABLE.replace("link_ops_t", "link_operations_t").replace(
    "struct link_ops {", "struct link_operations {")

DOC_AGREEMENT = PRELUDE + """
/**
 * Documents a code it cannot return, and omits one it can.
 * \\retval KATHERINE_E_OK on success.
 * \\retval KATHERINE_E_STATE never, in fact.
 */
katherine_error_t
mismatched(int x)
{
    if (x) return KATHERINE_E_NOMEM;
    return KATHERINE_E_OK;
}
"""


class Fixture:
    """A throwaway tree the indexer can be pointed at like a real repo."""

    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix="errdoc-selftest-")
        self.saved = (ed.REPO, ed.DB)
        build = os.path.join(self.dir, "build")
        os.makedirs(build)
        ed.REPO = self.dir
        ed.DB = os.path.join(build, "compile_commands.json")

    def write(self, name, body):
        path = os.path.join(self.dir, name)
        with open(path, "w") as fh:
            fh.write(body)
        with open(ed.DB, "w") as fh:
            json.dump([{"directory": self.dir,
                        "command": "cc -std=c11 -c %s -o %s.o" % (path, path),
                        "file": path}], fh)
        return path

    def index(self, name, body):
        return ed.build([self.write(name, body)])

    def close(self):
        ed.REPO, ed.DB = self.saved
        shutil.rmtree(self.dir, ignore_errors=True)


# ------------------------------------------------------------- indexing ---

def test_direct_and_propagated(fx):
    idx = fx.index("chain.c", DIRECT_AND_CHAIN)

    check("a direct return is recorded against the function that makes it",
          "KATHERINE_E_TIMEOUT" in idx.direct["leaf"], True)
    check("a caller does not claim a code directly",
          "KATHERINE_E_TIMEOUT" in idx.direct["middle"], False)

    # Propagation is the whole reason the tool exists: the code reaches top()
    # through two hops that neither raise nor name it.
    check("a code propagates two hops up the call graph",
          "KATHERINE_E_TIMEOUT" in idx.total["top"], True)
    check("a caller's own code stays its own",
          "KATHERINE_E_INVAL" in idx.direct["top"], True)
    check("a callee does not inherit its caller's codes",
          "KATHERINE_E_INVAL" in idx.total["leaf"], False)

    # The witness is what a description is written from, so it has to name
    # the hops rather than just assert the code is reachable.
    check("the witness names every hop down to the raising function",
          idx.witness("top", "KATHERINE_E_TIMEOUT"), ["top", "middle", "leaf"])
    check("a function raising a code is its own witness",
          idx.witness("leaf", "KATHERINE_E_TIMEOUT"), ["leaf"])


def test_vtable_expansion(fx):
    # The case this tool was extended for: without the vtable scan the chain
    # stops at the slot and link_send() looks like it cannot fail.
    idx = fx.index("vtable.c", VTABLE)

    check("the operation table's slot is mapped to its implementation",
          "udp_send" in idx.vtable.get("send", set()), True)
    check("a call through the table carries the implementation's codes",
          "KATHERINE_E_IO" in idx.total.get("link_send", set()), True)
    check("the witness goes through the implementation",
          idx.witness("link_send", "KATHERINE_E_IO"), ["link_send", "udp_send"])


def test_vtable_suffix_is_load_bearing(fx):
    idx = fx.index("vtable_suffix.c", VTABLE_WRONG_SUFFIX)

    # Not a defect to fix here -- a record of the coupling. If this ever
    # starts passing, the scanner got smarter and this case should say so.
    check("a table whose typedef does not end in _ops_t is not recognized",
          idx.vtable.get("send", set()), set())
    check("and the wrapper through it then claims no codes at all",
          idx.total.get("link_send", set()), set())


def test_doc_agreement(fx):
    idx = fx.index("doc.c", DOC_AGREEMENT)

    check("a documented code is read out of the doc block",
          idx.documented("mismatched"),
          {"KATHERINE_E_OK", "KATHERINE_E_STATE"})
    check("the docstring's claim of a code the function cannot return is "
          "visible as a difference",
          idx.documented("mismatched") - idx.total["mismatched"],
          {"KATHERINE_E_STATE"})
    check("a code the function returns and does not document is visible too",
          idx.total["mismatched"] - idx.documented("mismatched"),
          {"KATHERINE_E_NOMEM"})


def main():
    test_err_marker()
    test_retval_extraction()

    fx = Fixture()
    try:
        test_direct_and_propagated(fx)
        test_vtable_expansion(fx)
        test_vtable_suffix_is_load_bearing(fx)
        test_doc_agreement(fx)
    finally:
        fx.close()

    for f in FAILED:
        print("FAIL %s" % f)
    print("%d checks, %d failed" % (RAN, len(FAILED)))
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
