#!/usr/bin/env python3
# Generated sweep of the Python bindings' whole surface.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

"""Constructs every type the 'katherine' module exports, reads every
property on it and calls every method that takes no arguments.

smoke.py checks what the bindings are supposed to do, in 180 assertions
somebody wrote. This checks what they do at all, and it writes its own list
from the module's introspection -- so a type added tomorrow is swept
tomorrow, with nothing to remember. The failure it exists for is the one
nobody writes a test for: a property that reads a field of an object its
__cinit__ never filled, which is a read of uninitialised memory and, on a
pointer, a crash.

Surviving is half the result. The other half is that a property must either
return a value or raise one of the library's own errors; an AttributeError
or a TypeError out of a property is the binding disagreeing with itself.

Usage: sweep.py [module-dir]

[module-dir] is prepended to sys.path so the freshly built module is found
before an installed one, the same contract smoke.py has. It needs no daemon
and no readout: a type that cannot be constructed without one is reported
as skipped rather than exercised, and those are smoke.py's to cover.
"""

import inspect
import os
import sys

# Dunders are Python's, not the binding's. Everything else on an instance is
# surface a caller can reach.
def public(name):
    return not name.startswith('_')


class Tap:
    """Minimal TAP producer, as in smoke.py."""

    def __init__(self):
        self.count = 0
        self.failed = 0

    def check(self, description, cond, detail=None):
        self.count += 1
        if cond:
            print('ok %d - %s' % (self.count, description))
        else:
            self.failed += 1
            line = 'not ok %d - %s' % (self.count, description)
            if detail is not None:
                line += ' (%s)' % detail
            print(line)
        sys.stdout.flush()


def inherited_from_builtins(cls):
    """Names the object has because Python gave them to it.

    katherine.Timeout is a TimeoutError as well as a katherine.Error, so it
    carries OSError's attributes -- and OSError.characters_written raises
    AttributeError when nothing set it, which is CPython's behaviour and not
    the binding's. Sweeping only what the binding defines keeps the check
    about the binding.
    """
    names = set()
    for base in cls.__mro__:
        if base.__module__ == 'builtins':
            names |= set(vars(base))
    return names


def sweep_instance(katherine, obj):
    """Reads every property and calls every no-argument method.

    Returns the complaints. An error the library raises is an answer, not a
    complaint -- a getter on an object with nothing behind it yet is
    entitled to say so. Anything else is the binding failing to be a binding.
    """
    complaints = []
    skip = inherited_from_builtins(type(obj))
    for name in sorted(n for n in dir(obj) if public(n) and n not in skip):
        try:
            value = getattr(obj, name)
        except katherine.Error as e:
            continue
        except BaseException as e:
            complaints.append('%s: %s on read' % (name, type(e).__name__))
            continue

        if not callable(value):
            # Reading is the test; rendering it is the second half, because
            # a __repr__ is where a half-filled struct usually shows.
            try:
                repr(value)
            except BaseException as e:
                complaints.append('%s: %s on repr' % (name, type(e).__name__))
            continue

        try:
            signature = inspect.signature(value)
        except (TypeError, ValueError):
            # A built-in method the interpreter cannot describe; calling it
            # blind could pass the wrong arity, so it is left alone.
            continue

        if any(p.default is p.empty and p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD)
               for p in signature.parameters.values()):
            continue

        try:
            value()
        except katherine.Error:
            pass
        except BaseException as e:
            complaints.append('%s(): %s' % (name, type(e).__name__))
    return complaints


def main(argv):
    module_dir = argv[1] if len(argv) > 1 else os.environ.get('KATHERINE_PYTHON_MODULE_DIR')
    if module_dir:
        sys.path.insert(0, module_dir)

    import katherine

    tap = Tap()
    classes = [(n, getattr(katherine, n)) for n in sorted(dir(katherine))
               if public(n) and inspect.isclass(getattr(katherine, n))]
    tap.check('the module exports types to sweep', len(classes) > 0, '%d found' % len(classes))

    swept = 0
    needs_argument = []
    for name, cls in classes:
        # An enumeration is not constructed, it is enumerated. Its members
        # are where the C stringifiers are reached, which is the half of the
        # surface a constructor sweep would walk straight past.
        members = getattr(cls, '__members__', None)
        if members:
            complaints = []
            for member in members.values():
                complaints += ['%s.%s' % (member.name, c) for c in sweep_instance(katherine, member)]
                try:
                    str(member)
                except BaseException as e:
                    complaints.append('%s: %s on str' % (member.name, type(e).__name__))
            swept += 1
            tap.check('%s: all %d member(s) read and render' % (name, len(members)),
                      not complaints, '; '.join(complaints))
            continue

        # The binding's exceptions carry the code that raised them, so they
        # are constructed from one rather than from nothing.
        if isinstance(cls, type) and issubclass(cls, katherine.Error):
            constructor = lambda: cls(next(iter(katherine.ErrorCode)))
        else:
            constructor = cls

        try:
            obj = constructor()
        except TypeError:
            # Constructed from something this sweep does not have -- a
            # device, a link, an acquisition's five arguments. smoke.py
            # drives those against the daemon.
            needs_argument.append(name)
            continue
        except BaseException as e:
            tap.check('%s constructs' % name, False, type(e).__name__)
            continue

        swept += 1
        complaints = sweep_instance(katherine, obj)
        tap.check('%s: every property reads and every nullary method answers' % name,
                  not complaints, '; '.join(complaints))

    # Reported rather than asserted: which types need a fixture is a fact
    # about the surface, and a change in it should be visible without
    # failing anything.
    print('# swept %d type(s); %d need an argument to construct: %s'
          % (swept, len(needs_argument), ', '.join(needs_argument) or 'none'))

    for name in sorted(dir(katherine)):
        if not public(name):
            continue
        value = getattr(katherine, name)
        if inspect.isclass(value) or not inspect.isroutine(value):
            continue
        try:
            signature = inspect.signature(value)
        except (TypeError, ValueError):
            continue
        if any(p.default is p.empty and p.kind in (p.POSITIONAL_ONLY, p.POSITIONAL_OR_KEYWORD)
               for p in signature.parameters.values()):
            continue
        try:
            value()
            tap.check('%s() answers' % name, True)
        except katherine.Error:
            tap.check('%s() answers' % name, True)
        except BaseException as e:
            tap.check('%s() answers' % name, False, type(e).__name__)

    print('1..%d' % tap.count)
    return 1 if tap.failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
