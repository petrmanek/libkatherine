#!/usr/bin/env python3
# Example: enumerate a readout: chip ID, readout and comm status.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

import os
import sys
from katherine import Device


def parse_args(argv):
    address = None

    for arg in argv[1:]:
        if address is None:
            address = arg
        else:
            return None

    return address


def report_failure(step, err):
    print('kinfo: %s failed: %s' % (step, os.strerror(err)), file=sys.stderr)


def print_info(device):
    try:
        chip_id = device.get_chip_id()
    except OSError as e:
        report_failure('katherine_get_chip_id', e.errno)
        return False

    try:
        readout_status = device.get_readout_status()
    except OSError as e:
        report_failure('katherine_get_readout_status', e.errno)
        return False

    try:
        comm_status = device.get_comm_status()
    except OSError as e:
        report_failure('katherine_get_comm_status', e.errno)
        return False

    print('%-15s: %s' % ('Chip ID', chip_id))
    print('')
    print('Readout status')
    print('  %-13s: %d' % ('HW type', readout_status.hw_type))
    print('  %-13s: %d' % ('HW revision', readout_status.hw_revision))
    print('  %-13s: %d' % ('Serial number', readout_status.hw_serial_number))
    print('  %-13s: %d' % ('FW version', readout_status.fw_version))
    print('')
    print('Comm status')
    print('  %-13s: 0x%02x' % ('Comm lines', comm_status.comm_lines_mask))
    print('  %-13s: %d' % ('Data rate', comm_status.data_rate))
    print('  %-13s: %s' % ('Chip detected', 'yes' if comm_status.chip_detected else 'no'))

    return True


def main():
    address = parse_args(sys.argv)
    if address is None:
        print('Usage: kinfo <address>', file=sys.stderr)
        sys.exit(1)

    try:
        device = Device(address)
    except OSError as e:
        report_failure('katherine_device_init', e.errno)
        sys.exit(1)

    sys.exit(0 if print_info(device) else 1)


if __name__ == '__main__':
    main()
