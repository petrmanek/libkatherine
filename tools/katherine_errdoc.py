#!/usr/bin/env python3
"""Index the error codes libkatherine's functions can return.

Every function that reports failure returns a katherine_error_t. Which codes a
given function can actually produce is spread across its own returns and the
returns of everything it calls, so keeping that written down by hand does not
survive refactoring. This works it out from the source instead.

The index is built in two passes:

  1. Direct codes. A function that names KATHERINE_E_* in a return statement
     produces that code itself.
  2. Propagated codes. A function that returns the result of another function,
     or returns a variable that was assigned from one, produces whatever that
     callee produces. Resolving this is a fixed-point over the call graph --
     the call graph has cycles in principle, so iterating to a fixed point is
     what terminates rather than a topological walk.

Parsing is libclang's, not ours: the C is read through the same frontend the
compiler uses, with the project's real compile flags from
build/compile_commands.json, so macros and includes resolve as they do in a
build.

The build must have been configured with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON,
which puts compile_commands.json in the build directory.

Usage:
    tools/katherine_errdoc.py --check     report docstrings that disagree
    tools/katherine_errdoc.py --list      print the index
    tools/katherine_errdoc.py --diff      propose docstring edits as a patch

Exit status is 1 under --check when any docstring disagrees with the index, so
this can gate a build.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import defaultdict

try:
    import clang.cindex as ci
except ImportError:
    sys.exit("this tool needs python-clang (clang.cindex)")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(REPO, "build", "compile_commands.json")

# The marker that says a function reports failure through its return value.
ERR_MARKER = re.compile(r"\\return\s+Error code\.")
RETVAL = re.compile(r"\\retval\s+(KATHERINE_E_[A-Z_]+)")
CODE = re.compile(r"\bKATHERINE_E_[A-Z_]+\b")


def clang_builtin_includes():
    """Clang's own builtin headers.

    The compilation database is written for the compiler that configured the
    build, which is normally GCC. libclang does not inherit GCC's idea of where
    stddef.h lives, so without this every translation unit fails to find the
    freestanding headers and whole functions parse away to nothing -- silently,
    since a parse error still yields a partial AST.
    """
    import subprocess
    try:
        out = subprocess.run(["clang", "-print-resource-dir"],
                             capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        return []
    inc = os.path.join(out, "include")
    return ["-isystem", inc] if os.path.isdir(inc) else []


BUILTIN = clang_builtin_includes()


def compile_args(db, path):
    """Flags for path, falling back to any C entry so headers still parse."""
    for e in db:
        if os.path.abspath(e["file"]) == os.path.abspath(path):
            args = e["command"].split()[1:]
            return BUILTIN + [a for a in args if a not in ("-c", "-o") and not a.endswith(".o")
                              and os.path.abspath(a) != os.path.abspath(path)]
    for e in db:
        if e["file"].endswith(".c"):
            args = e["command"].split()[1:]
            return BUILTIN + [a for a in args if a not in ("-c", "-o") and not a.endswith(".o")
                              and not a.endswith(".c")]
    return []


def walk(node, fn):
    fn(node)
    for c in node.get_children():
        walk(c, fn)


def returns_in(fn_node):
    """Return statements of a function, as cursors."""
    out = []

    def visit(n):
        if n.kind == ci.CursorKind.RETURN_STMT:
            out.append(n)

    walk(fn_node, visit)
    return out


def tokens_of(node):
    """Tokens of a node, bounded to its own extent.

    Unbounded, get_tokens() on a function produced by a macro runs past the
    expansion and on through whatever follows in the header -- hundreds of
    tokens belonging to other functions, whose error codes would then be
    credited to a one-line forwarder. Filtering by line keeps only what the
    node actually spans.
    """
    lo, hi = node.extent.start.line, node.extent.end.line
    out = []
    for t in node.get_tokens():
        f = t.location.file
        if f is None or not (lo <= t.location.line <= hi):
            continue
        out.append(t.spelling)
    return out


def returned_names(fn_node):
    """Variables this function returns, by name.

    Most of this library reports failure as `res = KATHERINE_E_X; goto err;`
    with a single `return res;` at the bottom, so the code never appears in a
    return statement at all. Knowing which variables reach a return is what
    makes those assignments attributable.
    """
    names = set()
    for r in returns_in(fn_node):
        for n in r.walk_preorder():
            if n.kind == ci.CursorKind.DECL_REF_EXPR:
                names.add(n.spelling)
    return names


def calls_with_sites(fn_node):
    """(callee, file:line) for calls whose result can reach this function's return.

    Only those calls propagate an error code. A result that is discarded --
    `(void) katherine_udp_mutex_unlock(...)` is the common case here -- cannot,
    and counting it would credit a function with codes it can never return.
    The two shapes that do propagate are the same ones direct_codes() looks
    for: a call inside a return, and a call assigned to a variable that a
    return names.
    """
    returned = returned_names(fn_node)
    out = []

    def collect(scope):
        for n in scope.walk_preorder():
            if n.kind == ci.CursorKind.CALL_EXPR and n.referenced is not None:
                out.append((n.referenced.spelling, loc_of(n)))

    for n in fn_node.walk_preorder():
        if n.kind == ci.CursorKind.RETURN_STMT:
            collect(n)
            continue

        if n.kind != ci.CursorKind.BINARY_OPERATOR:
            continue

        kids = list(n.get_children())
        if len(kids) != 2:
            continue
        lhs = kids[0]
        if lhs.kind != ci.CursorKind.DECL_REF_EXPR or lhs.spelling not in returned:
            continue

        toks = tokens_of(n)
        nlhs = len(tokens_of(lhs))
        if nlhs >= len(toks) or toks[nlhs] != "=":
            continue

        collect(kids[1])

    return out


# Calls that are this library's own, as opposed to the platform's. Anything
# else reached from an error path is a candidate syscall to cite.
OURS = re.compile(r"^(katherine_|map_|recv_|send_|reverse_|acquisition_|handle_|flush_|pmd_)")

# Statement kinds whose controlling expression is what decided to fail.
GUARDS = (
    ci.CursorKind.IF_STMT,
    ci.CursorKind.WHILE_STMT,
    ci.CursorKind.DO_STMT,
    ci.CursorKind.SWITCH_STMT,
)


def loc_of(node):
    """file:line for a cursor, repo-relative."""
    f = node.location.file
    if f is None:
        return "?"
    return "%s:%d" % (os.path.relpath(f.name, REPO), node.location.line)


def parents_of(fn_node):
    """Parent map for one function; libclang exposes no parent pointer."""
    parent = {}
    stack = [fn_node]
    while stack:
        cur = stack.pop()
        for kid in cur.get_children():
            parent[kid.hash] = cur
            stack.append(kid)
    return parent


# Evidence that an error path derives from an OS error rather than from an
# argument check. Without this test every guard in the library looks as though
# it had an errno behind it.
OS_ERROR = ("errno", "last_os_error", "map_syscall_error", "WSAGetLastError")

# The errno macro expands to a call; it is not the failing operation.
NOT_A_SYSCALL = ("__errno_location", "_errno")

# Compiler and libc internals -- byte swaps, builtins, inline shims. They are
# not operations with a manual page, so citing them would mislead.
INTERNAL = re.compile(r"^(__|_[A-Z])")


def consults_os_error(node, parent):
    """Whether this error path reads an OS error at all."""
    scopes = [node]
    cur = node
    depth = 0
    while cur is not None and depth < 8:
        up = parent.get(cur.hash)
        if up is not None and up.kind in GUARDS + (ci.CursorKind.COMPOUND_STMT,):
            scopes.append(up)
        cur = up
        depth += 1
    for scope in scopes:
        text = " ".join(tokens_of(scope))
        if any(m in text for m in OS_ERROR):
            return True
    return False


def syscalls_near(node, parent):
    """Platform calls that could have set the errno this error path reports.

    The idiom throughout is

        if (setsockopt(...) == -1) {
            u->last_os_error = errno;
            res = map_syscall_error(errno, ...);

    so the call that failed is in the controlling expression of the enclosing
    if. Where a result was captured first and tested afterwards, the enclosing
    block is searched as well. Naming it lets a description send the reader to
    the right manual page instead of to errno in the abstract.
    """
    found, seen = [], set()

    def collect(scope):
        for n in scope.walk_preorder():
            if n.kind != ci.CursorKind.CALL_EXPR:
                continue
            name = n.spelling
            if not name or OURS.match(name) or name in seen:
                continue
            if name in NOT_A_SYSCALL or INTERNAL.match(name):
                continue
            seen.add(name)
            found.append(name)

    cur = node
    depth = 0
    while cur is not None and depth < 8:
        up = parent.get(cur.hash)
        if up is not None and up.kind in GUARDS:
            kids = list(up.get_children())
            if kids:
                collect(kids[0])
            if found:
                return found
        if up is not None and up.kind == ci.CursorKind.COMPOUND_STMT:
            collect(up)
            if found:
                return found
        cur = up
        depth += 1
    return found


def direct_codes(fn_node):
    """Codes this function produces itself, rather than passing on.

    Returns {code: {"sites": [file:line], "syscalls": [name]}}.

    Two shapes count, and both are read off the AST rather than by splitting
    the token stream: clang's tokens include comments and preprocessor lines,
    so a statement is not simply a run of tokens up to a semicolon.

      return KATHERINE_E_X;       the code is in a return statement
      res = KATHERINE_E_X;        assigned to a variable that a return uses,
                                  which is how most of this library reports
                                  failure -- via `goto err` to a single exit
    """
    out = defaultdict(lambda: {"sites": [], "syscalls": []})
    returned = returned_names(fn_node)
    parent = parents_of(fn_node)

    def record(node, codes):
        if not codes:
            return
        where = loc_of(node)
        calls = syscalls_near(node, parent) if consults_os_error(node, parent) else []
        for c in codes:
            e = out[c]
            if where not in e["sites"]:
                e["sites"].append(where)
            for s in calls:
                if s not in e["syscalls"]:
                    e["syscalls"].append(s)

    for n in fn_node.walk_preorder():
        if n.kind == ci.CursorKind.RETURN_STMT:
            record(n, CODE.findall(" ".join(tokens_of(n))))
            continue

        if n.kind != ci.CursorKind.BINARY_OPERATOR:
            continue

        kids = list(n.get_children())
        if len(kids) != 2:
            continue
        lhs = kids[0]
        if lhs.kind != ci.CursorKind.DECL_REF_EXPR or lhs.spelling not in returned:
            continue

        toks = tokens_of(n)
        nlhs = len(tokens_of(lhs))
        if nlhs >= len(toks) or toks[nlhs] != "=":
            continue

        record(n, CODE.findall(" ".join(tokens_of(kids[1]))))

    return out


def macro_code_map(tu):
    """macro name -> error codes its definition contains, resolved through
    macros that expand other macros.

    A function generated by a macro has no tokens of its own to read, so its
    codes have to come from the macro body. DEFINE_ACQ_IMPL_EVERY_SHIFT
    expands DEFINE_ACQ_IMPL, which is where the codes actually are, hence the
    transitive step.
    """
    codes, refs = {}, {}
    for n in tu.cursor.walk_preorder():
        if n.kind != ci.CursorKind.MACRO_DEFINITION:
            continue
        toks = [t.spelling for t in n.get_tokens()]
        joined = " ".join(toks)
        codes[n.spelling] = set(CODE.findall(joined))
        refs[n.spelling] = {t for t in toks if t in codes or t.isupper()}

    def resolve(name, seen):
        if name in seen:
            return set()
        seen.add(name)
        acc = set(codes.get(name, ()))
        for r in refs.get(name, ()):
            if r in codes:
                acc |= resolve(r, seen)
        return acc

    return {m: resolve(m, set()) for m in codes}


def macro_sites(tu):
    """(file, line) -> macro names instantiated there."""
    out = {}
    for n in tu.cursor.walk_preorder():
        if n.kind != ci.CursorKind.MACRO_INSTANTIATION or n.location.file is None:
            continue
        out.setdefault((os.path.abspath(n.location.file.name), n.location.line),
                       []).append(n.spelling)
    return out


class Index:
    def __init__(self):
        self.direct = defaultdict(set)      # name -> {code}
        self.detail = {}                    # name -> {code: {sites, syscalls}}
        self.calls = defaultdict(set)       # name -> {callee}
        self.sites = defaultdict(list)      # (caller, callee) -> [file:line]
        self.doc = {}                    # name -> (file, line, comment)
        self.is_err = set()              # names whose docstring claims a code
        self.defined = set()

    def scan(self, tu):
        self._macro_codes = macro_code_map(tu)
        self._macro_sites = macro_sites(tu)
        for node in tu.cursor.walk_preorder():
            if node.kind != ci.CursorKind.FUNCTION_DECL:
                continue
            if not node.is_definition():
                # a declaration still carries the docstring in headers
                self._record_doc(node)
                continue
            name = node.spelling
            self.defined.add(name)
            self._record_doc(node)

            detail = direct_codes(node)

            # A single-line definition in this codebase means a macro made it,
            # since the house style puts the return type on its own line. Its
            # codes are in the macro body, not at the expansion site.
            if node.extent.start.line == node.extent.end.line and node.location.file:
                key = (os.path.abspath(node.location.file.name), node.extent.start.line)
                for m in self._macro_sites.get(key, ()):
                    for c in self._macro_codes.get(m, ()):
                        entry = detail.setdefault(
                            c, {"sites": [], "syscalls": []})
                        where = "%s (via %s)" % (loc_of(node), m)
                        if where not in entry["sites"]:
                            entry["sites"].append(where)
            self.detail.setdefault(name, {}).update(detail)
            self.direct[name] |= set(detail)

            # Propagation: which functions this one calls, and where. The
            # locations are what let a reader jump straight to the hop that
            # introduced a code rather than searching for the call.
            for callee, where in calls_with_sites(node):
                self.calls[name].add(callee)
                if where not in self.sites[(name, callee)]:
                    self.sites[(name, callee)].append(where)

    def _record_doc(self, node):
        raw = node.raw_comment
        if not raw:
            return
        prev = self.doc.get(node.spelling)
        # Prefer the comment at the definition, which is where this project
        # keeps them; a header declaration's comment is the fallback.
        if prev is None or node.is_definition():
            loc = node.location
            self.doc[node.spelling] = (loc.file.name if loc.file else "?", loc.line, raw)
        if ERR_MARKER.search(raw) or RETVAL.search(raw):
            self.is_err.add(node.spelling)

    def resolve(self):
        """Fixed point over the call graph."""
        total = {k: set(v) for k, v in self.direct.items()}
        changed = True
        rounds = 0
        while changed:
            changed = False
            rounds += 1
            for name in sorted(self.calls):
                acc = set(total.get(name, ()))
                for callee in sorted(self.calls[name]):
                    # No gate on which callees count: one with no codes of its
                    # own contributes an empty set, and gating on a set that
                    # grows during iteration would make the result depend on
                    # the order files happened to be parsed in.
                    acc |= total.get(callee, set())
                if acc != total.get(name, set()):
                    total[name] = acc
                    changed = True
            if rounds > 100:
                break
        self.total = total
        self.rounds = rounds
        return total

    def witness(self, name, code):
        """Shortest call chain from name to a function returning code directly.

        This is what makes a bespoke description possible: the chain names the
        intermediate calls, so "KATHERINE_E_IO" can be documented as what it
        means *here* rather than as what the enumerator says in general.
        """
        from collections import deque
        if code in self.direct.get(name, ()):
            return [name]
        seen = {name}
        q = deque([(name, [name])])
        while q:
            cur, path = q.popleft()
            for callee in sorted(self.calls.get(cur, ())):
                if callee in seen:
                    continue
                if code not in self.total.get(callee, ()):
                    continue
                seen.add(callee)
                nxt = path + [callee]
                if code in self.direct.get(callee, ()):
                    return nxt
                q.append((callee, nxt))
        return []

    def documented(self, name):
        entry = self.doc.get(name)
        if not entry:
            return set()
        return set(RETVAL.findall(entry[2]))


def build(paths):
    if not os.path.exists(DB):
        sys.exit("no %s -- configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON first"
                 % os.path.relpath(DB, REPO))
    db = json.load(open(DB))
    index = Index()
    idx = ci.Index.create()
    for p in paths:
        args = compile_args(db, p)
        tu = idx.parse(p, args=args, options=ci.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
        fatal = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
        if fatal:
            print("  ! %s: %d parse error(s), first: %s" % (p, len(fatal), fatal[0].spelling),
                  file=sys.stderr)
        index.scan(tu)
    index.resolve()
    return index


def sources():
    out = []
    for root, _dirs, files in os.walk(os.path.join(REPO, "c", "src")):
        for f in files:
            if f.endswith(".c"):
                out.append(os.path.join(root, f))
    return sorted(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--list", action="store_true", help="print the index")
    g.add_argument("--check", action="store_true", help="report disagreeing docstrings")
    g.add_argument("--diff", action="store_true", help="propose docstring edits")
    g.add_argument("--paths", action="store_true",
                   help="print, for each code, the call chain that produces it")
    args = ap.parse_args()

    index = build(sources())

    names = sorted(n for n in index.total if index.total[n] and n in index.defined)

    if args.paths:
        for n in names:
            if n not in index.is_err:
                continue
            f, line, _ = index.doc.get(n, ("?", 0, ""))
            print("%s  (%s:%d)" % (n, os.path.relpath(f, REPO), line))
            for code in sorted(index.total[n]):
                chain = index.witness(n, code)
                origin = chain[-1] if chain else n
                detail = index.detail.get(origin, {}).get(code, {})

                if len(chain) <= 1:
                    where = ", ".join(detail.get("sites", [])) or "?"
                    print("    %-22s raised here          %s" % (code, where))
                else:
                    # Each hop with the line the call is made on, so the chain
                    # can be walked in an editor without searching.
                    hops = []
                    for a, b in zip(chain, chain[1:]):
                        at = index.sites.get((a, b), ["?"])[0]
                        hops.append("%s (%s)" % (b, at))
                    print("    %-22s %s" % (code, " -> ".join(hops)))
                    where = ", ".join(detail.get("sites", [])) or "?"
                    print("    %-22s raised in %s at %s" % ("", origin, where))

                # map_syscall_error and its kin only translate an errno they
                # were handed, so the operation that failed is named one hop
                # further back. Walk the chain until something names one.
                calls = []
                for hop in reversed(chain or [n]):
                    calls = index.detail.get(hop, {}).get(code, {}).get("syscalls", [])
                    if calls:
                        break
                if not calls and len(chain) > 1:
                    # The origin only translated an errno it was given, and the
                    # code it produced is not one the caller raises by name, so
                    # look at what the calling function's error paths consult.
                    caller = chain[-2]
                    seen = []
                    for d in index.detail.get(caller, {}).values():
                        for c in d.get("syscalls", []):
                            if c not in seen:
                                seen.append(c)
                    calls = seen
                if calls:
                    print("    %-22s errno from: %s" % ("", ", ".join(calls)))
            print()
        return 0

    if args.list:
        print("# error codes by function (fixed point reached in %d round(s))" % index.rounds)
        for n in names:
            marker = "" if n in index.is_err else "   [no \\return Error code. marker]"
            print("%-52s %s%s" % (n, " ".join(sorted(index.total[n])), marker))
        return 0

    bad = 0
    for n in names:
        if n not in index.is_err:
            continue
        have = index.documented(n)
        want = index.total[n] | {"KATHERINE_E_OK"}
        if have != want:
            bad += 1
            f, line, _ = index.doc.get(n, ("?", 0, ""))
            print("%s:%d: %s" % (os.path.relpath(f, REPO), line, n))
            if want - have:
                print("    missing: %s" % " ".join(sorted(want - have)))
            if have - want:
                print("    unreachable: %s" % " ".join(sorted(have - want)))
    if args.check:
        print("\n%d function(s) disagree with the index" % bad)
        return 1 if bad else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
