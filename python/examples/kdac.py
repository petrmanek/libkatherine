#!/usr/bin/env python3
"""Example: inspect and drive the chip's DACs.

The same command line as c/examples/kdac.c and cxx/examples/kdacxx.cpp, and
the same bytes on stdout, so that --json output from the three can be
compared against one another.

Copyright (c) 2018 Petr Manek.
This software is distributed under the terms of the MIT License, copied
verbatim in the file "LICENSE".

SPDX-License-Identifier: MIT
"""

import sys

import katherine

# The counts come from the library, as they do in the other two
# implementations: how many DACs a chip has is its business, not this file's.
DAC_COUNT = katherine.TPX3_DAC_COUNT
MONITOR_COUNT = katherine.TPX3_DAC_MONITOR_COUNT


def fold(text):
    """Case, hyphens, underscores and spaces are all the same character when
    a name is matched: one monitor really is called "BandGap output", and a
    name needing shell quoting is not one to insist on."""
    return text.lower().replace('-', '_').replace(' ', '_')


def find_dac(name):
    for dac in katherine.Tpx3Dac:
        if fold(name) == fold(str(dac)):
            return dac
    return None


def find_monitor(name):
    for i in range(MONITOR_COUNT):
        if fold(name) == fold(_monitor_name(i)):
            return _monitor_at(i)
    return None


def _monitor_at(index):
    """The first eighteen monitors are the DACs themselves; the last four are
    Tpx3DacMonitor members."""
    if index < DAC_COUNT:
        return list(katherine.Tpx3Dac)[index]
    return list(katherine.Tpx3DacMonitor)[index - DAC_COUNT]


def _monitor_name(index):
    return str(_monitor_at(index))


def unit_symbol(unit):
    return str(unit)


def usage(argv0):
    sys.stderr.write(
        "usage: {0} [options] <command> [arguments]\n"
        "\n"
        "commands:\n"
        "  list                      every DAC of the chip, with its index, maximum and unit\n"
        "  monitor [<dac>]           the voltage at one DAC monitor, or at all of them\n"
        "  set <dac> <value>         write one DAC, then latch it into the chip\n"
        "  set <v0> <v1> ... <v17>   write all {1}, which is faster per DAC\n"
        "  scan <dac> [<lo> <hi>]    sweep a DAC and read its monitor at every point\n"
        "\n"
        "options:\n"
        "  --address <host>          readout to talk to; required by every command but list\n"
        "  --chip <n>                chip index to address; required by every command but list\n"
        "  --asic <type>             list only: which ASIC's DACs to print, e.g. tpx3\n"
        "  --si                      print setpoints converted to amperes or volts\n"
        "  --json                    readings to stdout as JSON, diagnostics to stderr\n"
        "\n"
        "A readout reports the voltage at a DAC's monitor and never the setpoint\n"
        "written to it, so `monitor` is named for what it reads. A single `set`\n"
        "composes with whatever the readout last held, which a power cycle clears;\n"
        "write all {1} to establish a baseline you know.\n".format(argv0, DAC_COUNT))


class Args:
    def __init__(self):
        self.command = 'list'
        self.address = None
        self.asic = None
        self.chip_index = 0
        self.si = False
        self.json = False
        self.values = []


def parse_args(argv):
    args = Args()
    positional = []
    chip = None

    i = 1
    while i < len(argv):
        arg = argv[i]

        if arg == '--si':
            args.si = True
        elif arg == '--json':
            args.json = True
        elif arg == '--address':
            i += 1
            if i >= len(argv):
                return None
            args.address = argv[i]
        elif arg.startswith('--address='):
            args.address = arg[len('--address='):]
        elif arg.startswith('--asic='):
            args.asic = arg[len('--asic='):]
        elif arg == '--asic':
            i += 1
            if i >= len(argv):
                return None
            args.asic = argv[i]
        elif arg.startswith('--chip='):
            chip = arg[len('--chip='):]
        elif arg == '--chip':
            i += 1
            if i >= len(argv):
                return None
            chip = argv[i]
        elif len(arg) > 1 and arg.startswith('-'):
            sys.stderr.write('unknown option "{0}"\n'.format(arg))
            return None
        else:
            positional.append(arg)
        i += 1

    if not positional:
        return None

    if positional[0] not in ('list', 'monitor', 'set', 'scan'):
        sys.stderr.write('unknown command "{0}"\n'.format(positional[0]))
        return None
    args.command = positional[0]

    # --asic names a chip type and reaches no readout; --chip names an index
    # and addresses one. Which of them a command takes is known only now, so
    # this is where the other one is refused.
    if args.command == 'list':
        if chip is not None:
            sys.stderr.write('list reads a DAC table and addresses no chip; '
                             'it takes --asic <type>\n')
            return None
        if args.asic is None:
            args.asic = 'tpx3'
    elif args.asic is not None:
        sys.stderr.write('--asic names a chip type and is taken only by list\n')
        return None
    elif chip is None:
        # No default. A readout with two chips would otherwise take a DAC
        # write meant for one and apply it to the other, with nothing in the
        # command to say which was meant.
        sys.stderr.write('--chip <n> is required by {0}; this readout may have '
                         'more than one chip\n'.format(args.command))
        return None
    else:
        try:
            parsed = int(chip, 10)
            if parsed < 0 or parsed > 255:
                raise ValueError
        except ValueError:
            sys.stderr.write('--chip takes a chip index, not "{0}"\n'.format(chip))
            return None
        args.chip_index = parsed

    args.values = positional[1:]
    return args


def fmt_g(value):
    """%.6g as C prints it, which is what the three outputs are compared on."""
    return '{0:.6g}'.format(value)


def parse_value(text):
    try:
        value = int(text, 0)
    except ValueError:
        return None
    if value < 0 or value > 0xFFFF:
        return None
    return value


def cmd_list(args):
    if fold(args.asic) != 'tpx3':
        sys.stderr.write(
            'no DAC table for asic "{0}"; this library knows tpx3\n'.format(args.asic))
        return 1

    if args.json:
        sys.stdout.write('[')

    for i, dac in enumerate(katherine.Tpx3Dac):
        maximum = dac.max
        max_si, unit = dac.to_si(maximum)

        if args.json:
            sys.stdout.write(
                '{0}\n  {{"index":{1},"name":"{2}","max":{3},"max_si":{4},"unit":"{5}"}}'.format(
                    '' if i == 0 else ',', i, str(dac), maximum, fmt_g(max_si),
                    unit_symbol(unit)))
        elif args.si:
            sys.stdout.write('{0:2d}  {1:<20}  max {2:4d}  {3} {4}\n'.format(
                i, str(dac), maximum, fmt_g(max_si), unit_symbol(unit)))
        else:
            sys.stdout.write('{0:2d}  {1:<20}  max {2:4d}  {3}\n'.format(
                i, str(dac), maximum, unit_symbol(unit)))

    if args.json:
        sys.stdout.write('\n]\n')

    return 0


def cmd_monitor(device, args):
    if args.values:
        monitor = find_monitor(args.values[0])
        if monitor is None:
            sys.stderr.write(
                'no DAC monitor called "{0}"; try `kdac list`\n'.format(args.values[0]))
            return 1

        voltage = device.get_dac_monitor_voltage(args.chip_index, monitor)

        if args.json:
            sys.stdout.write('{{"monitor":"{0}","volt":{1}}}\n'.format(
                str(monitor), fmt_g(voltage)))
        else:
            sys.stdout.write('{0:<20}  {1:.4f} V\n'.format(str(monitor), voltage))

        return 0

    voltages = device.get_dac_monitor_voltages(args.chip_index)

    if args.json:
        sys.stdout.write('[')

    for i in range(MONITOR_COUNT):
        volt = voltages[i]

        if args.json:
            sys.stdout.write('{0}\n  {{"index":{1},"monitor":"{2}","volt":{3}}}'.format(
                '' if i == 0 else ',', i, _monitor_name(i), fmt_g(volt)))
        else:
            sys.stdout.write('{0:2d}  {1:<20}  {2:.4f} V\n'.format(i, _monitor_name(i), volt))

    if args.json:
        sys.stdout.write('\n]\n')

    return 0


def cmd_set(device, args):
    if len(args.values) == DAC_COUNT:
        dacs = katherine.Tpx3Dacs()

        for i, text in enumerate(args.values):
            value = parse_value(text)
            if value is None:
                sys.stderr.write('value {0} ("{1}") is not a DAC code\n'.format(i, text))
                return 1
            dacs[i] = value

        device.set_dacs(args.chip_index, dacs)

        if not args.json:
            sys.stdout.write('wrote all {0} DACs of chip {1}\n'.format(
                DAC_COUNT, args.chip_index))

        return 0

    if len(args.values) != 2:
        sys.stderr.write('set takes a DAC and a value, or all {0} values\n'.format(DAC_COUNT))
        return 1

    dac = find_dac(args.values[0])
    if dac is None:
        sys.stderr.write('no DAC called "{0}"; try `kdac list`\n'.format(args.values[0]))
        return 1

    value = parse_value(args.values[1])
    if value is None:
        sys.stderr.write('"{0}" is not a DAC code\n'.format(args.values[1]))
        return 1

    maximum = dac.max
    if value > maximum:
        sys.stderr.write('{0} holds at most {1}, not {2}\n'.format(str(dac), maximum, value))
        return 1

    device.set_dac(args.chip_index, dac, value)

    if args.json:
        return 0

    if args.si:
        si, unit = dac.to_si(value)
        sys.stdout.write('{0} = {1} ({2} {3}) on chip {4}\n'.format(
            str(dac), value, fmt_g(si), unit_symbol(unit), args.chip_index))
    else:
        sys.stdout.write('{0} = {1} on chip {2}\n'.format(str(dac), value, args.chip_index))

    return 0


def cmd_scan(device, args):
    if len(args.values) not in (1, 3):
        sys.stderr.write('scan takes a DAC, optionally with a first and a last code\n')
        return 1

    dac = find_dac(args.values[0])
    if dac is None:
        sys.stderr.write('no DAC called "{0}"; try `kdac list`\n'.format(args.values[0]))
        return 1

    maximum = dac.max
    first, last = 0, maximum

    if len(args.values) == 3:
        first = parse_value(args.values[1])
        last = parse_value(args.values[2])
        if first is None or last is None:
            sys.stderr.write('a scan range is two DAC codes\n')
            return 1
        if first > last or last > maximum:
            sys.stderr.write('{0} scans between 0 and {1}\n'.format(str(dac), maximum))
            return 1

    if args.json:
        sys.stdout.write('[')

    for code in range(first, last + 1):
        device.set_dac(args.chip_index, dac, code)
        voltage = device.get_dac_monitor_voltage(args.chip_index, dac)
        si, unit = dac.to_si(code)

        if args.json:
            sys.stdout.write(
                '{0}\n  {{"code":{1},"setpoint_si":{2},"unit":"{3}","volt":{4}}}'.format(
                    '' if code == first else ',', code, fmt_g(si), unit_symbol(unit),
                    fmt_g(voltage)))
        elif args.si:
            sys.stdout.write('{0:4d}  {1:>12} {2}  {3:.4f} V\n'.format(
                code, fmt_g(si), unit_symbol(unit), voltage))
        else:
            sys.stdout.write('{0:4d}  {1:.4f} V\n'.format(code, voltage))

    if args.json:
        sys.stdout.write('\n]\n')

    return 0


def main(argv):
    args = parse_args(argv)
    if args is None:
        usage(argv[0])
        return 2

    if args.command == 'list':
        return cmd_list(args)

    if args.address is None:
        sys.stderr.write('{0} needs --address for this command\n'.format(argv[0]))
        return 2

    device = katherine.Device(args.address)

    # Opening succeeds whether or not the readout answered: the enumeration
    # inside it is best-effort. Saying so here beats letting the first
    # per-chip call report a state error instead.
    if not device.derived_info.supported:
        sys.stderr.write('no supported readout answered at {0}\n'.format(args.address))
        return 1

    if args.command == 'monitor':
        return cmd_monitor(device, args)
    if args.command == 'set':
        return cmd_set(device, args)
    return cmd_scan(device, args)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
