#!/usr/bin/env python3
# Example: periodically monitor readout and chip temperature.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

import os
import sys
import time
from katherine import Device


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
    readout_temp = 0.0
    chip_temp = 0.0

    try:
        if show_readout:
            readout_temp = device.get_readout_temperature()

        if show_chip:
            chip_temp = device.get_sensor_temperature()
    except OSError as e:
        return False, os.strerror(e.errno)

    if show_readout and show_chip:
        print('readout: %.1f C, chip: %.1f C' % (readout_temp, chip_temp))
    elif show_readout:
        print('readout: %.1f C' % readout_temp)
    else:
        print('chip: %.1f C' % chip_temp)

    sys.stdout.flush()
    return True, None


def main():
    args = parse_args(sys.argv)
    if args is None:
        print('Usage: ktemp [--readout] [--chip] [--period <seconds>] <address>', file=sys.stderr)
        sys.exit(1)

    show_readout, show_chip, period, address = args

    try:
        device = Device(address)
    except OSError:
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
