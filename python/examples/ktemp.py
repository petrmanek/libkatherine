#!/usr/bin/env python3
# Example: periodically monitor readout and chip temperature.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

import sys

from kargs import wants_help
import time
from katherine import Device, Error


def parse_args(argv):
    show_readout = False
    show_chip = False
    period = 1.0  # seconds
    address = None

    i = 1
    while i < len(argv):
        arg = argv[i]

        if arg == '--readout':
            show_readout = True
        elif arg == '--chip':
            show_chip = True
        elif arg == '--period':
            i += 1
            if i >= len(argv):
                return None
            try:
                period = float(argv[i])
            except ValueError:
                return None
            if period <= 0:
                return None
        elif address is None:
            address = arg
        else:
            return None

        i += 1

    if address is None:
        return None

    if not show_readout and not show_chip:
        show_readout = True
        show_chip = True

    return show_readout, show_chip, period, address


def poll_temperatures(device, show_readout, show_chip):
    parts = []

    try:
        if show_readout:
            parts.append('readout: %.1f C' % device.get_readout_temperature())

        # A temperature per chip: they are separate sensors and read
        # differently, so one number cannot stand for a readout carrying
        # several. The readout's own stays where it is, not being a chip's.
        if show_chip:
            for chip in range(device.info.chip_count):
                parts.append('chip %d: %.1f C' % (chip, device.get_sensor_temperature(chip)))
    except Error as e:
        return False, str(e)

    print(', '.join(parts))
    sys.stdout.flush()
    return True, None


def usage(out):
    """To stdout for --help and to stderr for a usage error."""
    print('Usage: ktemp [--readout] [--chip] [--period <seconds>] <address>', file=out)
    print('  e.g. ktemp --chip udp://192.168.1.42', file=out)


def main():
    if wants_help(sys.argv):
        usage(sys.stdout)
        return

    args = parse_args(sys.argv)
    if args is None:
        usage(sys.stderr)
        sys.exit(1)

    show_readout, show_chip, period, address = args

    try:
        device = Device(address)
    except Error:
        print('ktemp: no response from %s' % address, file=sys.stderr)
        sys.exit(1)

    had_success = False
    try:
        while True:
            ok, err = poll_temperatures(device, show_readout, show_chip)
            if ok:
                had_success = True
            elif not had_success:
                print('ktemp: no response from %s' % address, file=sys.stderr)
                sys.exit(1)
            else:
                print('ktemp: no response from %s: %s' % (address, err))
                sys.stdout.flush()

            time.sleep(period)
    except KeyboardInterrupt:
        sys.exit(0)


if __name__ == '__main__':
    main()
