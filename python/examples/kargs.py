"""The one argument every example answers the same way.

@author Petr Mánek
@date 27.10.26

@copyright Copyright (c) 2018 Petr Mánek.
This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".

SPDX-License-Identifier: MIT
"""


def wants_help(argv):
    """Whether the command line asks for the usage text.

    Scanned across every argument rather than tested at argv[1]: an example
    whose first argument is an address is still asked for help by
    `ktemp.py --chip --help`, and a reader who types it there means it."""
    return any(arg in ('-h', '--help') for arg in argv[1:])
