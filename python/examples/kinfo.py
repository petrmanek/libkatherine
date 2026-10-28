#!/usr/bin/env python3
# Example: enumerate a readout: chip ID, readout and comm status.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

import sys

from kargs import wants_help
from katherine import Device, DeviceFlags, Error


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
        readout_status = device.get_readout_status()
    except Error as e:
        report_failure('katherine_get_readout_status', e)
        return False

    try:
        comm_status = device.get_comm_status()
    except Error as e:
        report_failure('katherine_get_comm_status', e)
        return False

    derived = device.derived_info

    print('Device information')
    print('  %-16s: %s' % ('Name', derived.name if derived.name is not None else '(unknown)'))
    print('  %-16s: %s' % ('Chip type', derived.chip_type))
    print('  %-16s: %d' % ('Generation', derived.gen))
    print('  %-16s: %d' % ('Max chips', derived.max_chip_count))
    print('  %-16s: %d' % ('Bias supplies', derived.bias_supply_count))
    print('  %-16s: %d' % ('Total GPIO', derived.all_gpio_count))
    print('  %-16s: %d' % ('Accessible GPIO', derived.accessible_gpio_count))
    print('  %-16s: %d' % ('TDC channels', derived.tdc_channel_count))
    print('  %-16s: %d' % ('Pulse channels', derived.pulse_channel_count))
    print('  %-16s: %s' % ('Supported', 'true' if derived.supported else 'false'))
    print('')
    print('Readout status')
    print('  %-16s: %d' % ('HW type', readout_status.hw_type))
    print('  %-16s: %d' % ('HW revision', readout_status.hw_revision))
    print('  %-16s: %d' % ('Serial number', readout_status.serial_number))
    print('  %-16s: %d' % ('FW version', readout_status.fw_version))
    print('')
    print('Comm status')
    print('  %-16s: 0x%02x' % ('Comm lines', comm_status.comm_lines_mask))
    print('  %-16s: %d Mb/s' % ('Data rate', comm_status.data_rate))
    print('  %-16s: %d' % ('Chip count', comm_status.chip_count))
    print('')

    # One line per chip the readout reports, rather than chip 0 and the
    # impression that a readout has one.
    print('Detected chips')
    for chip in range(device.info.chip_count):
        try:
            print('  Chip %-11d: %s' % (chip, device.get_chip_id(chip)))
        except Error as e:
            report_failure('katherine_get_chip_id', e)
            return False

    return True


def usage(out):
    """To stdout for --help and to stderr for a usage error, which is what
    lets `kinfo.py --help | less` work and a mistake still show up in a
    pipeline."""
    print('Usage: kinfo <address>', file=out)
    print('  e.g. kinfo udp://192.168.1.42', file=out)


def main():
    if wants_help(sys.argv):
        usage(sys.stdout)
        return

    address = parse_args(sys.argv)
    if address is None:
        usage(sys.stderr)
        sys.exit(1)

    try:
        # This combination of flags means that the readout will not be configured for extensive work, but it will also not overwrite any previous configuration.
        flags = DeviceFlags.DONT_CHANGE_UDP_PORTS | DeviceFlags.DONT_SELECT_INTERFACE

        device = Device(address, flags)
    except Error as e:
        report_failure('katherine_device_init', e)
        sys.exit(1)

    sys.exit(0 if print_info(device) else 1)


if __name__ == '__main__':
    main()
