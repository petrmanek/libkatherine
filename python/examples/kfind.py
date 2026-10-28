#!/usr/bin/env python3
# Example: locate Katherine readouts in a given IP address range.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

import sys

from kargs import wants_help
from functools import reduce
from katherine import Device, DeviceFlags

def format_ip(a, b=None, c=None, d=None):        
    return '%d.%d.%d.%d' % (tuple(a) if isinstance(a, list) else (a, b, c, d))


def check_address(a0, a1, a2, a3):
    address = 'udp://' + format_ip(a0, a1, a2, a3)
    chip_id = None

    try:
        # This combination of flags means that the readout will not be configured for extensive work, but it will also not overwrite any previous configuration.
        flags = DeviceFlags.DONT_CHANGE_UDP_PORTS | DeviceFlags.DONT_SELECT_INTERFACE

        dev = Device(address, flags)
        chip_id = dev.get_chip_id(0)
    except:
        return False

    print('Found device: %s,\t chip id: %s' % (address, chip_id))
    return True


def usage(out):
    """To stdout for --help and to stderr for a usage error."""
    print('Usage: %s [<address range>]' % sys.argv[0], file=out)
    print('  e.g. %s 192.168.1.100-200' % sys.argv[0], file=out)
    print('  Each of the four octets may be a number or a lo-hi range.', file=out)
    print('  With no argument, scans 192.168.1.100-200.', file=out)


def refuse(message):
    print('%s: error parsing address range, %s' % (sys.argv[0], message), file=sys.stderr)
    usage(sys.stderr)
    sys.exit(1)


def parse_range(component):
    # One dotted component, either `n` or `n-m`, as the C and C++ examples read it.
    lo, dash, hi = component.partition('-')
    try:
        return (int(lo), int(hi) if dash else int(lo))
    except ValueError:
        refuse('invalid range specified')


def parse_args(argv, mins, maxs):
    if len(argv) < 2:
        return

    components = argv[1].split('.')
    if len(components) != 4:
        refuse('expected 3 dots (got %d)' % (len(components) - 1))

    for i, component in enumerate(components):
        mins[i], maxs[i] = parse_range(component)


def search_range():
    if wants_help(sys.argv):
        usage(sys.stdout)
        return 0

    mins = [192, 168, 1, 100]
    maxs = [192, 168, 1, 200]
    parse_args(sys.argv, mins, maxs)

    n_hosts = reduce(lambda x, y: x*y, [maxs[i] - mins[i] + 1 for i in range(4)])
    print('Testing %d hosts.\t Start: %s,\t End: %s' % (n_hosts, format_ip(mins), format_ip(maxs)))

    n_attempted = 0
    n_found = 0

    for a0 in range(mins[0], maxs[0]+1):
        for a1 in range(mins[1], maxs[1]+1):
            for a2 in range(mins[2], maxs[2]+1):
                for a3 in range(mins[3], maxs[3]+1):
                    found = check_address(a0, a1, a2, a3)
                    n_attempted += 1
                    n_found += 1 if found else 0

    print('Done. Tested %d hosts (%d found).' % (n_attempted, n_found))
    return 0 if n_found > 0 else 1


if __name__ == '__main__':
    sys.exit(search_range())
