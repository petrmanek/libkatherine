/**
 * \file
 * \brief Example: inspect and drive the chip's DACs.
 * \author Petr Mánek
 * \date 15.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <katherinexx/katherinexx.hpp>

namespace {

constexpr int set_all_argc = KATHERINE_TPX3_DAC_COUNT;

enum class command {
    list,
    monitor,
    set,
    scan
};

struct kdac_args {
    command cmd{command::list};
    std::string address;
    std::string asic;
    bool have_asic{false};
    std::uint8_t chip_index{0};
    bool si{false};
    bool json{false};
    std::vector<std::string> values;
};

// Case, hyphens, underscores and spaces are all the same character here, as
// in the C implementation: one monitor really is called "BandGap output".
char
fold(char c)
{
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    if (c == '-' || c == ' ') return '_';

    return c;
}

bool
name_matches(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;

    for (std::size_t i = 0; i < a.size(); ++i)
        if (fold(a[i]) != fold(b[i])) return false;

    return true;
}

bool
find_dac(const std::string& name, katherine_tpx3_dac_t& out)
{
    for (int i = 0; i < KATHERINE_TPX3_DAC_COUNT; ++i) {
        const auto dac = static_cast<katherine_tpx3_dac_t>(i);
        if (name_matches(name, katherine_tpx3_dac_name(dac))) {
            out = dac;
            return true;
        }
    }

    return false;
}

bool
find_monitor(const std::string& name, katherine_tpx3_dac_monitor_t& out)
{
    for (int i = 0; i < KATHERINE_TPX3_DAC_MONITOR_COUNT; ++i) {
        const auto monitor = static_cast<katherine_tpx3_dac_monitor_t>(i);
        if (name_matches(name, katherine_tpx3_dac_monitor_name(monitor))) {
            out = monitor;
            return true;
        }
    }

    return false;
}

const char *
unit_symbol(katherine_dac_unit_t unit)
{
    return unit == KATHERINE_DAC_UNIT_AMP ? "A" : "V";
}

bool
parse_value(const std::string& text, std::uint16_t& out)
{
    char *end             = nullptr;
    const std::uint64_t v = std::strtoull(text.c_str(), &end, 0);

    if (end == text.c_str() || *end != '\0' || v > 0xFFFFUL) return false;

    out = static_cast<std::uint16_t>(v);
    return true;
}

void
usage(const char *argv0)
{
    std::fprintf(stderr,
        "usage: %s [options] <command> [arguments]\n"
        "\n"
        "commands:\n"
        "  list                      every DAC of the chip, with its index, maximum and unit\n"
        "  monitor [<dac>]           the voltage at one DAC monitor, or at all of them\n"
        "  set <dac> <value>         write one DAC, then latch it into the chip\n"
        "  set <v0> <v1> ... <v17>   write all %d, which is faster per DAC\n"
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
        "write all %d to establish a baseline you know.\n",
        argv0, set_all_argc, set_all_argc);
}

bool
parse_args(int argc, char *argv[], kdac_args& args)
{
    std::vector<std::string> positional;
    std::string chip;
    bool have_chip = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg{argv[i]};

        if (arg == "--si") {
            args.si = true;
        } else if (arg == "--json") {
            args.json = true;
        } else if (arg == "--address") {
            if (++i >= argc) return false;
            args.address = argv[i];
        } else if (arg.rfind("--address=", 0) == 0) {
            args.address = arg.substr(10);
        } else if (arg.rfind("--asic=", 0) == 0) {
            args.asic      = arg.substr(7);
            args.have_asic = true;
        } else if (arg == "--asic") {
            if (++i >= argc) return false;
            args.asic      = argv[i];
            args.have_asic = true;
        } else if (arg.rfind("--chip=", 0) == 0) {
            chip      = arg.substr(7);
            have_chip = true;
        } else if (arg == "--chip") {
            if (++i >= argc) return false;
            chip      = argv[i];
            have_chip = true;
        } else if (arg.size() > 1 && arg[0] == '-') {
            std::fprintf(stderr, "unknown option \"%s\"\n", arg.c_str());
            return false;
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.empty()) return false;

    const std::string& name = positional[0];
    if (name == "list") {
        args.cmd = command::list;
    } else if (name == "monitor") {
        args.cmd = command::monitor;
    } else if (name == "set") {
        args.cmd = command::set;
    } else if (name == "scan") {
        args.cmd = command::scan;
    } else {
        std::fprintf(stderr, "unknown command \"%s\"\n", name.c_str());
        return false;
    }

    // --asic names a chip type and reaches no readout; --chip names an index
    // and addresses one. Which of them a command takes is known only now, so
    // this is where the other one is refused.
    if (args.cmd == command::list) {
        if (have_chip) {
            std::fprintf(stderr, "list reads a DAC table and addresses no chip; it takes --asic <type>\n");
            return false;
        }
        if (!args.have_asic) args.asic = "tpx3";
    } else if (args.have_asic) {
        std::fprintf(stderr, "--asic names a chip type and is taken only by list\n");
        return false;
    } else if (!have_chip) {
        // No default. A readout with two chips would otherwise take a DAC
        // write meant for one and apply it to the other, with nothing in the
        // command to say which was meant.
        std::fprintf(stderr, "--chip <n> is required by %s; this readout may have more than one chip\n", name.c_str());
        return false;
    } else {
        char *end             = nullptr;
        const std::uint64_t v = std::strtoull(chip.c_str(), &end, 10);
        if (end == chip.c_str() || *end != '\0' || v > 255) {
            std::fprintf(stderr, "--chip takes a chip index, not \"%s\"\n", chip.c_str());
            return false;
        }
        args.chip_index = static_cast<std::uint8_t>(v);
    }

    args.values.assign(positional.begin() + 1, positional.end());

    return true;
}

// ------------------------------------------------------------------

int
cmd_list(const kdac_args& args)
{
    if (!name_matches(args.asic, "tpx3")) {
        std::fprintf(stderr, "no DAC table for asic \"%s\"; this library knows tpx3\n",
            args.asic.c_str());
        return 1;
    }

    if (args.json) std::printf("[");

    for (int i = 0; i < KATHERINE_TPX3_DAC_COUNT; ++i) {
        const auto dac          = static_cast<katherine_tpx3_dac_t>(i);
        const std::uint16_t max = katherine::tpx3::dac_max(dac);
        const auto max_si       = katherine::tpx3::dac_to_si(dac, max);

        if (args.json) {
            std::printf(
                "%s\n  {\"index\":%d,\"name\":\"%s\",\"max\":%u,\"max_si\":%.6g,\"unit\":\"%s\"}",
                i == 0 ? "" : ",", i, katherine_tpx3_dac_name(dac), max, max_si.value,
                unit_symbol(max_si.unit));
        } else if (args.si) {
            std::printf("%2d  %-20s  max %4u  %.6g %s\n", i, katherine_tpx3_dac_name(dac), max,
                max_si.value, unit_symbol(max_si.unit));
        } else {
            std::printf(
                "%2d  %-20s  max %4u  %s\n", i, katherine_tpx3_dac_name(dac), max,
                unit_symbol(max_si.unit));
        }
    }

    if (args.json) std::printf("\n]\n");

    return 0;
}

int
cmd_monitor(katherine::device& device, const kdac_args& args)
{
    if (!args.values.empty()) {
        katherine_tpx3_dac_monitor_t monitor;
        if (!find_monitor(args.values[0], monitor)) {
            std::fprintf(stderr, "no DAC monitor called \"%s\"; try `kdac list`\n",
                args.values[0].c_str());
            return 1;
        }

        const float voltage = device.dac_monitor_voltage(args.chip_index, monitor);

        if (args.json) {
            std::printf("{\"monitor\":\"%s\",\"volt\":%.6g}\n",
                katherine_tpx3_dac_monitor_name(monitor), static_cast<double>(voltage));
        } else {
            std::printf("%-20s  %.4f V\n", katherine_tpx3_dac_monitor_name(monitor),
                static_cast<double>(voltage));
        }

        return 0;
    }

    const katherine_tpx3_dac_voltages_t voltages = device.dac_monitor_voltages(args.chip_index);

    if (args.json) std::printf("[");

    for (int i = 0; i < KATHERINE_TPX3_DAC_MONITOR_COUNT; ++i) {
        const auto monitor = static_cast<katherine_tpx3_dac_monitor_t>(i);

        if (args.json) {
            std::printf("%s\n  {\"index\":%d,\"monitor\":\"%s\",\"volt\":%.6g}", i == 0 ? "" : ",",
                i, katherine_tpx3_dac_monitor_name(monitor),
                static_cast<double>(voltages.array[i]));
        } else {
            std::printf("%2d  %-20s  %.4f V\n", i, katherine_tpx3_dac_monitor_name(monitor),
                static_cast<double>(voltages.array[i]));
        }
    }

    if (args.json) std::printf("\n]\n");

    return 0;
}

int
cmd_set(katherine::device& device, const kdac_args& args)
{
    if (static_cast<int>(args.values.size()) == set_all_argc) {
        katherine::tpx3::dacs dacs{};

        for (int i = 0; i < set_all_argc; ++i) {
            if (!parse_value(args.values[static_cast<std::size_t>(i)], dacs.array[i])) {
                std::fprintf(stderr, "value %d (\"%s\") is not a DAC code\n", i,
                    args.values[static_cast<std::size_t>(i)].c_str());
                return 1;
            }
        }

        device.set_dacs(args.chip_index, dacs);

        if (!args.json)
            std::printf("wrote all %d DACs of chip %u\n", set_all_argc,
                static_cast<unsigned>(args.chip_index));

        return 0;
    }

    if (args.values.size() != 2) {
        std::fprintf(stderr, "set takes a DAC and a value, or all %d values\n", set_all_argc);
        return 1;
    }

    katherine_tpx3_dac_t dac;
    if (!find_dac(args.values[0], dac)) {
        std::fprintf(stderr, "no DAC called \"%s\"; try `kdac list`\n", args.values[0].c_str());
        return 1;
    }

    std::uint16_t value;
    if (!parse_value(args.values[1], value)) {
        std::fprintf(stderr, "\"%s\" is not a DAC code\n", args.values[1].c_str());
        return 1;
    }

    const std::uint16_t max = katherine_tpx3_dac_max(dac);
    if (value > max) {
        std::fprintf(stderr, "%s holds at most %u, not %u\n", katherine_tpx3_dac_name(dac), max,
            value);
        return 1;
    }

    device.set_dac(args.chip_index, dac, value);

    if (args.json) return 0;

    if (args.si) {
        const auto si = katherine::tpx3::dac_to_si(dac, value);
        std::printf("%s = %u (%.6g %s) on chip %u\n", katherine_tpx3_dac_name(dac), value, si.value,
            unit_symbol(si.unit), static_cast<unsigned>(args.chip_index));
    } else {
        std::printf("%s = %u on chip %u\n", katherine_tpx3_dac_name(dac), value,
            static_cast<unsigned>(args.chip_index));
    }

    return 0;
}

int
cmd_scan(katherine::device& device, const kdac_args& args)
{
    if (args.values.size() != 1 && args.values.size() != 3) {
        std::fprintf(stderr, "scan takes a DAC, optionally with a first and a last code\n");
        return 1;
    }

    katherine_tpx3_dac_t dac;
    if (!find_dac(args.values[0], dac)) {
        std::fprintf(stderr, "no DAC called \"%s\"; try `kdac list`\n", args.values[0].c_str());
        return 1;
    }

    const std::uint16_t max = katherine_tpx3_dac_max(dac);
    std::uint16_t first = 0, last = max;

    if (args.values.size() == 3) {
        if (!parse_value(args.values[1], first) || !parse_value(args.values[2], last)) {
            std::fprintf(stderr, "a scan range is two DAC codes\n");
            return 1;
        }
        if (first > last || last > max) {
            std::fprintf(stderr, "%s scans between 0 and %u\n", katherine_tpx3_dac_name(dac), max);
            return 1;
        }
    }

    const auto monitor = static_cast<katherine_tpx3_dac_monitor_t>(dac);

    if (args.json) std::printf("[");

    for (std::uint32_t code = first; code <= last; ++code) {
        device.set_dac(args.chip_index, dac, static_cast<std::uint16_t>(code));
        const float voltage = device.dac_monitor_voltage(args.chip_index, monitor);

        const auto si = katherine::tpx3::dac_to_si(dac, static_cast<std::uint16_t>(code));

        if (args.json) {
            std::printf("%s\n  {\"code\":%u,\"setpoint_si\":%.6g,\"unit\":\"%s\",\"volt\":%.6g}",
                code == first ? "" : ",", code, si.value, unit_symbol(si.unit),
                static_cast<double>(voltage));
        } else if (args.si) {
            std::printf("%4u  %12.6g %s  %.4f V\n", code, si.value, unit_symbol(si.unit),
                static_cast<double>(voltage));
        } else {
            std::printf("%4u  %.4f V\n", code, static_cast<double>(voltage));
        }
    }

    if (args.json) std::printf("\n]\n");

    return 0;
}

} // namespace

int
main(int argc, char *argv[])
{
    kdac_args args;

    if (!parse_args(argc, argv, args)) {
        usage(argv[0]);
        return 2;
    }

    if (args.cmd == command::list) return cmd_list(args);

    if (args.address.empty()) {
        std::fprintf(stderr, "%s needs --address for this command\n", argv[0]);
        return 2;
    }

    try {
        katherine::device device{args.address};

        // Opening succeeds whether or not the readout answered: the
        // enumeration inside it is best-effort. Saying so here beats letting
        // the first per-chip call report a state error instead.
        if (!device.derived_info().supported) {
            std::fprintf(stderr, "no supported readout answered at %s\n", args.address.c_str());
            return 1;
        }

        switch (args.cmd) {
        case command::monitor: return cmd_monitor(device, args);
        case command::set:     return cmd_set(device, args);
        case command::scan:    return cmd_scan(device, args);
        default:               return 2;
        }
    } catch (const katherine::error& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
