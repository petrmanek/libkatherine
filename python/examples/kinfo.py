#!/usr/bin/env python3
# Example: enumerate a readout: chip ID, readout and comm status.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

import sys
from katherine import Device

# The katherine C extension raises one of these for a device call that
# failed with a library error code (see katherine.pyx's check_return_code());
# each is constructed from katherine_strerror(), so str(e) is already the
# message a caller wants to show.
KATHERINE_ERRORS = (TimeoutError, ValueError, MemoryError, RuntimeError)


def parse_args(argv):
    address = None

    for arg in argv[1:]:
        if address is None:
            address = arg
        else:
            return None

    return address


def report_failure(step, err):
    print('kinfo: %s failed: %s' % (step, err), file=sys.stderr)


def print_info(device):
    try:
        chip_id = device.get_chip_id()
    except KATHERINE_ERRORS as e:
        report_failure('katherine_get_chip_id', e)
        return False

    try:
        readout_status = device.get_readout_status()
    except KATHERINE_ERRORS as e:
        report_failure('katherine_get_readout_status', e)
        return False

    try:
        comm_status = device.get_comm_status()
    except KATHERINE_ERRORS as e:
        report_failure('katherine_get_comm_status', e)
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
    print('  %-13s: %d Mb/s' % ('Data rate', comm_status.data_rate))
    print('  %-13s: %d' % ('Chips', comm_status.chip_count))

    return True


def main():
    address = parse_args(sys.argv)
    if address is None:
        print('Usage: kinfo <address>', file=sys.stderr)
        sys.exit(1)

    try:
        device = Device(address)
    except KATHERINE_ERRORS as e:
        report_failure('katherine_device_init', e)
        sys.exit(1)

    sys.exit(0 if print_info(device) else 1)


if __name__ == '__main__':
    main()
