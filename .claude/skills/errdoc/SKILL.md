---
name: errdoc
description: Document which katherine_error_t codes a function can return, as \retval tables, one module at a time. Use when asked to write or refresh error-code documentation in libkatherine, or when tools/katherine_errdoc.py --check reports disagreements.
---

# Documenting error codes

Every libkatherine function that can fail returns a `katherine_error_t`. This
task adds, to each such function's doc block, a `\retval` line per code it can
actually return, describing what that code means **for that function** rather
than restating the enumerator.

`tools/katherine_errdoc.py` works out which codes reach which function by
parsing the sources with libclang. It cannot write the descriptions: those
require reading the code at the cited line and saying something true about it.
That division is the point — the tool supplies the facts, you supply the prose.

## Before starting

The tool needs a compilation database:

```
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

**Serialize test runs before running any.** The suite binds fixed UDP ports, so
wrap every `ctest` and every hardware probe in `flock <scratchpad>/ksim.lock
<command>`. Two runs at once fail for reasons that have nothing to do with your
change.

Record the baseline you must not regress:

```
cmake --build build -j"$(nproc)"                        # must exit 0
flock <scratchpad>/ksim.lock ctest --test-dir build     # note the test count
rm -f build/docs/docs.stamp
cmake --build build --target docs 2>&1 | grep -c warning:
```

At the time of writing the suite is 20 tests and Doxygen emits 314 warnings,
all pre-existing. Take your own baseline rather than trusting those numbers:
the `grep -c` above is the number to beat, and the `rm` is required because the
target is stamp-guarded and will otherwise print nothing at all.

## Scope one module per commit

Work through one `.c` file at a time and commit it. `tools/katherine_errdoc.py
--check` lists the functions still disagreeing, grouped by file; pick one file
from that list. A single commit spanning the whole library is unreviewable.

Commit subject prefix is `c:` for anything under `c/`. Take the prefix from
`git log`, never from a roadmap.

## Getting the facts

```
python3 tools/katherine_errdoc.py --paths      # call chains behind each code
python3 tools/katherine_errdoc.py --check      # docstrings that disagree
python3 tools/katherine_errdoc.py --list       # the bare index
```

`--paths` gives, for each function and code, the chain of calls that produces
it, every hop with `file:line`, the site where the code is raised, and — where
the code came from an OS error — the system call that set it. One code of one
function looks like this; a real function has such a block per code:

```
katherine_device_enumerate  (c/src/device/device.c:199)
    KATHERINE_E_SYSTEM     katherine_get_readout_status (c/src/device/device.c:205)
                           -> katherine_udp_mutex_lock (c/src/device/status.c:33)
                           raised in katherine_udp_mutex_lock at c/src/transport/udp_nix.c:528
                           errno from: pthread_mutex_lock
```

The chain is the shortest route to that code, not the only one. Nothing stops a
second, longer route existing, so read the function's own error paths too
instead of assuming the chain enumerates them.

## Writing a description

**Open every cited line before writing about it.** The chain tells you where to
look; it does not tell you what the code means. A description written from the
enumerator's name alone is worthless — the reader already has the enumerator.

Good:

```
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the command
 *   within the control session's receive timeout.
```

Bad, because it restates the name:

```
 * \retval KATHERINE_E_TIMEOUT if the operation timed out.
```

Rules of thumb:

- Say **what failed**, not what the code is called. "if the readout did not
  answer" beats "on timeout".
- Where the tool names a system call, cite its manual page — `sendto(2)`,
  `pthread_mutex_lock(3)`, `inet_pton(3)`. That is where the reader finds the
  underlying errno values. Mention `katherine_udp_last_os_error()` when the
  function records one.
- A code reachable by more than one route gets **one** `\retval` line, since
  Doxygen renders one row per code and two rows for the same code read as a
  contradiction. Join two or three routes with "or". Beyond that, stop
  enumerating and name the category instead — "if any of the control-plane
  sends failed" with one representative manpage — because a line listing six
  system calls tells the reader less than a line naming what went wrong.
- `KATHERINE_E_OK` comes first and reads "on success."
- Keep it to a sentence or two. This is a reference, not an essay.

### Where the tool over-reports

Two cases to check rather than transcribe:

- **A pure mapper.** `map_syscall_error` translates an errno it was handed, so
  the codes it lists are whatever the mapping can produce, not what this caller
  can trigger. Read the caller's error paths and describe only the reachable
  ones.
- **A shared helper.** A code arriving through a widely used helper may be
  unreachable from this particular caller. If reading the code says a code
  cannot occur here, leave it out and say so in the commit message — the tool's
  `--check` will then disagree, and that disagreement is information, not a
  failure to fix by padding the docs.
- **A macro-generated function.** Much of the command layer is generated by
  `K_DEFINE_CMD_*`, and for those the cited `file:line` is the *expansion site*
  — a single line that contains no error handling to read. Open the macro
  definition instead; that is where the code actually comes from, and it is
  shared by every function the macro generates.

## The docstring itself

Replace the existing `\return Error code.` line with the `\retval` block.
`\retval` renders as a two-column table in the HTML output, which repeated
`\return` does not — repeated `\return` collapses into one prose paragraph.

```c
/**
 * Enumerate the readout, asking what hardware and firmware it is.
 * \param device Device to enumerate.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer within the
 *   control session's receive timeout.
 * \retval KATHERINE_E_SYSTEM if the control session's lock could not be
 *   taken; see pthread_mutex_lock(3) and katherine_udp_last_os_error().
 */
katherine_error_t
katherine_device_enumerate(katherine_device_t *device)
```

## House style, which is enforced

- **Doc blocks live at the definition in the `.c` file.** Public headers carry
  bare declarations. Do not add a doc block to a header; a duplicate there
  shadows the real one and Doxygen warns once they diverge.
- **Doxygen commands take a backslash**: `\param`, `\retval`, `\return`,
  `\brief`. Not `@param`.
- **Comment syntax**: `/** */` for doc blocks, `///` or `///<` for a
  declaration or struct field, `//` for prose inside a function. `/* */` only
  where `//` cannot work — an inline label with code after it, a designated
  initializer annotation, or inside a line-continued `#define`, where `//`
  would swallow the backslash and truncate the macro.
- Struct field comments stay in the header, as `///<` after the member.

## Do not insert between a comment and its subject

Adding lines directly above a function's signature puts them between the
preceding doc block and the function it documents. Anchor above the `/**`, not
on the signature. Then run:

```
python3 tools/check_insertions.py --cached
```

It must print `clean` before you commit. This mistake has been made repeatedly;
the check exists because the compiler is silent about it.

## Verifying

Run all of these, and judge each by its exit code:

```
git diff --name-only | grep -E '\.(c|h)$' | xargs -r clang-format -i
cmake --build build -j"$(nproc)"                        # exit 0
flock <scratchpad>/ksim.lock ctest --test-dir build     # exit 0, same count
rm -f build/docs/docs.stamp
cmake --build build --target docs 2>&1 | grep -c warning:   # <= baseline
python3 tools/katherine_errdoc.py --check               # your file gone from the list
python3 tools/check_insertions.py --cached              # clean
git diff --check                                        # no whitespace errors
```

`docs` is an `ALL` target, so the ordinary build runs Doxygen too and its log
holds both kinds of diagnostic. Counting `warning` across the whole build log
therefore reports hundreds of pre-existing Doxygen warnings and reads as though
the compiler were unhappy. Match on the compiler's own line format when you
want compiler diagnostics only:

```
grep -E '^/.*\.(c|h|hpp):[0-9]+:[0-9]+: (warning|error):' build.log | wc -l
```

**Never judge a build or a test run by the tail of its log.** Grep for the
summary line and report it verbatim, and check the exit code. A build that
fails leaves stale binaries, and `ctest` will then pass against old code —
this has produced a false green more than once.

Since this change only touches comments, the code must be untouched. Prove it
rather than assume it — `-fpreprocessed` strips comments while expanding
neither includes nor macros, and `-P` drops the line markers that would
otherwise report every shifted line as a difference:

```
strip() { gcc -x c -fpreprocessed -dD -E -P -; }
f=c/src/device/device.c
diff <(git show "HEAD:$f" | strip) <(strip < "$f")
```

`-x c` and the trailing `-` are both load-bearing: hand gcc `/dev/stdin` as a
filename instead and it decides the input is a linker script, prints nothing,
and the diff comes back empty — a false pass of exactly the kind this check is
meant to catch.

## Committing

The workflow is propose-only: stage the change, write the message, and let the
maintainer commit. Do not run `git commit` or `git push`.

Never run `git checkout --` on a file that has unstaged changes — it restores
from the index and destroys them. Check the second column of `git status`
first: ` M` or `MM` means unstaged work exists.

Message shape: what the module does, which codes turned out to be reachable
and anything surprising about them, and any code the tool claims that you
concluded is unreachable, with the reason.
