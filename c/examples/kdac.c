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

#include <stdbool.h> // bool
#include <stdint.h>
#include <stdio.h>  // printf, fprintf
#include <stdlib.h> // strtoull, exit
#include <string.h> // strcmp, strncmp

#include <katherine/katherine.h>

#include "kargs.h"

#define KDAC_SET_ALL_ARGC KATHERINE_TPX3_DAC_COUNT

typedef enum kdac_command {
    KDAC_LIST = 0,
    KDAC_MONITOR,
    KDAC_SET,
    KDAC_SCAN,
} kdac_command_t;

typedef struct kdac_args {
    kdac_command_t command;
    const char *address;
    const char *asic; ///< `list` only: which ASIC's DACs to print, named by chip type.
    uint8_t chip_index;
    bool si;
    bool json;

    /// Command arguments, in order, pointing into argv. Held here rather than
    /// in parse_args()'s frame, which the commands outlive.
    const char *values[KDAC_SET_ALL_ARGC + 1];
    int value_count;
    const char *subject; ///< First of them, or NULL where there are none.
} kdac_args_t;

// ------------------------------------------------------------------
// Names to indices. The library prints a name for an index; going the other
// way is the command line's problem, so it lives here.

// Case, hyphens, underscores and spaces are all the same character here.
// One monitor really is called "BandGap output", with a space, and a name
// needing shell quoting to type is not one a command line should insist on.
static char
fold(char c)
{
    if (c >= 'A' && c <= 'Z') return (char) (c - 'A' + 'a');
    if (c == '-' || c == ' ') return '_';

    return c;
}

static bool
name_matches(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; ++a, ++b)
        if (fold(*a) != fold(*b)) return false;

    return *a == *b;
}

static bool
find_dac(const char *name, katherine_tpx3_dac_t *out)
{
    for (int i = 0; i < KATHERINE_TPX3_DAC_COUNT; ++i) {
        if (name_matches(name, katherine_tpx3_dac_name((katherine_tpx3_dac_t) i))) {
            *out = (katherine_tpx3_dac_t) i;
            return true;
        }
    }

    return false;
}

static bool
find_monitor(const char *name, katherine_tpx3_dac_monitor_t *out)
{
    for (int i = 0; i < KATHERINE_TPX3_DAC_MONITOR_COUNT; ++i) {
        if (name_matches(name, katherine_tpx3_dac_monitor_name((katherine_tpx3_dac_monitor_t) i))) {
            *out = (katherine_tpx3_dac_monitor_t) i;
            return true;
        }
    }

    return false;
}

static const char *
unit_symbol(katherine_dac_unit_t unit)
{
    return unit == KATHERINE_DAC_UNIT_AMP ? "A" : "V";
}

// ------------------------------------------------------------------

// To stdout for --help and to stderr for a usage error, which is what lets
// `kdac --help | less` work and a mistake still show up in a pipeline.
static void
usage(FILE *out, const char *argv0)
{
    fprintf(out,
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
        argv0, KDAC_SET_ALL_ARGC, KDAC_SET_ALL_ARGC);
}

static bool
parse_args(int argc, char *argv[], kdac_args_t *args)
{
    memset(args, 0, sizeof(*args));

    // Options are taken wherever they appear, so that `kdac set --chip=2 Vfbk
    // 164` reads as naturally as `kdac --chip=2 set Vfbk 164`. What is left
    // over, in order, is the command and its arguments.
    const char *positional[KDAC_SET_ALL_ARGC + 2];
    const char *chip         = NULL;
    int positional_count     = 0;
    const int positional_max = (int) (sizeof(positional) / sizeof(positional[0]));

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--si") == 0) {
            args->si = true;
        } else if (strcmp(arg, "--json") == 0) {
            args->json = true;
        } else if (strcmp(arg, "--address") == 0) {
            if (++i >= argc) return false;
            args->address = argv[i];
        } else if (strncmp(arg, "--address=", 10) == 0) {
            args->address = arg + 10;
        } else if (strncmp(arg, "--asic=", 7) == 0) {
            args->asic = arg + 7;
        } else if (strcmp(arg, "--asic") == 0) {
            if (++i >= argc) return false;
            args->asic = argv[i];
        } else if (strncmp(arg, "--chip=", 7) == 0) {
            chip = arg + 7;
        } else if (strcmp(arg, "--chip") == 0) {
            if (++i >= argc) return false;
            chip = argv[i];
        } else if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "unknown option \"%s\"\n", arg);
            return false;
        } else {
            if (positional_count == positional_max) {
                fprintf(stderr, "too many arguments\n");
                return false;
            }
            positional[positional_count++] = arg;
        }
    }

    if (positional_count == 0) return false;

    const char *command = positional[0];
    if (strcmp(command, "list") == 0) {
        args->command = KDAC_LIST;
    } else if (strcmp(command, "monitor") == 0) {
        args->command = KDAC_MONITOR;
    } else if (strcmp(command, "set") == 0) {
        args->command = KDAC_SET;
    } else if (strcmp(command, "scan") == 0) {
        args->command = KDAC_SCAN;
    } else {
        fprintf(stderr, "unknown command \"%s\"\n", command);
        return false;
    }

    // `--asic` names a chip type and reaches no readout; `--chip` names an
    // index and addresses one. Which of them a command takes is known only
    // now, so this is where the other one is refused.
    if (args->command == KDAC_LIST) {
        if (chip != NULL) {
            fprintf(stderr, "list reads a DAC table and addresses no chip; it takes --asic <type>\n");
            return false;
        }
        if (args->asic == NULL) args->asic = "tpx3";
    } else if (args->asic != NULL) {
        fprintf(stderr, "--asic names a chip type and is taken only by list\n");
        return false;
    } else if (chip != NULL) {
        char *end       = NULL;
        uint64_t parsed = strtoull(chip, &end, 10);
        if (end == chip || *end != '\0' || parsed > 255) {
            fprintf(stderr, "--chip takes a chip index, not \"%s\"\n", chip);
            return false;
        }
        args->chip_index = (uint8_t) parsed;
    } else {
        // No default. A readout with two chips would otherwise take a DAC
        // write meant for one and apply it to the other, with nothing in the
        // command to say which was meant.
        fprintf(stderr, "--chip <n> is required by %s; this readout may have more than one chip\n", command);
        return false;
    }

    args->value_count = positional_count - 1;
    for (int v = 0; v < args->value_count; ++v) args->values[v] = positional[1 + v];
    if (args->value_count > 0) args->subject = args->values[0];

    return true;
}

// ------------------------------------------------------------------

static int
cmd_list(const kdac_args_t *args)
{
    if (!name_matches(args->asic, "tpx3")) {
        fprintf(stderr, "no DAC table for asic \"%s\"; this library knows tpx3\n", args->asic);
        return 1;
    }

    if (args->json) printf("[");

    for (int i = 0; i < KATHERINE_TPX3_DAC_COUNT; ++i) {
        const katherine_tpx3_dac_t dac = (katherine_tpx3_dac_t) i;
        const uint16_t max             = katherine_tpx3_dac_max(dac);

        katherine_dac_unit_t unit = KATHERINE_DAC_UNIT_VOLT;
        const double max_si       = katherine_tpx3_dac_to_si(dac, max, &unit);

        if (args->json) {
            printf("%s\n  {\"index\":%d,\"name\":\"%s\",\"max\":%u,\"max_si\":%.6g,\"unit\":\"%s\"}",
                i == 0 ? "" : ",", i, katherine_tpx3_dac_name(dac), max, max_si,
                unit_symbol(unit));
        } else if (args->si) {
            printf("%2d  %-20s  max %4u  %.6g %s\n", i, katherine_tpx3_dac_name(dac), max, max_si,
                unit_symbol(unit));
        } else {
            printf("%2d  %-20s  max %4u  %s\n", i, katherine_tpx3_dac_name(dac), max,
                unit_symbol(unit));
        }
    }

    if (args->json) printf("\n]\n");

    return 0;
}

static int
cmd_monitor(katherine_device_t *device, const kdac_args_t *args)
{
    if (args->subject != NULL) {
        katherine_tpx3_dac_monitor_t monitor;
        if (!find_monitor(args->subject, &monitor)) {
            fprintf(stderr, "no DAC monitor called \"%s\"; try `kdac list`\n", args->subject);
            return 1;
        }

        float voltage               = 0.f;
        const katherine_error_t res = katherine_tpx3_get_dac_monitor_voltage(device, args->chip_index, monitor, &voltage);
        if (res) {
            fprintf(stderr, "cannot read %s: %s\n", katherine_tpx3_dac_monitor_name(monitor),
                katherine_strerror(res));
            return 1;
        }

        if (args->json) {
            printf("{\"monitor\":\"%s\",\"volt\":%.6g}\n",
                katherine_tpx3_dac_monitor_name(monitor), (double) voltage);
        } else {
            printf("%-20s  %.4f V\n", katherine_tpx3_dac_monitor_name(monitor), (double) voltage);
        }

        return 0;
    }

    // All of them, which the readout answers in one command rather than
    // twenty-two round trips.
    katherine_tpx3_dac_voltages_t voltages;
    const katherine_error_t res = katherine_tpx3_get_dac_monitor_voltages(device, args->chip_index, &voltages);
    if (res) {
        fprintf(stderr, "cannot read the DAC monitors: %s\n", katherine_strerror(res));
        return 1;
    }

    if (args->json) printf("[");

    for (int i = 0; i < KATHERINE_TPX3_DAC_MONITOR_COUNT; ++i) {
        const katherine_tpx3_dac_monitor_t monitor = (katherine_tpx3_dac_monitor_t) i;

        if (args->json) {
            printf("%s\n  {\"index\":%d,\"monitor\":\"%s\",\"volt\":%.6g}", i == 0 ? "" : ",", i,
                katherine_tpx3_dac_monitor_name(monitor), (double) voltages.array[i]);
        } else {
            printf("%2d  %-20s  %.4f V\n", i, katherine_tpx3_dac_monitor_name(monitor),
                (double) voltages.array[i]);
        }
    }

    if (args->json) printf("\n]\n");

    return 0;
}

static bool
parse_value(const char *text, uint16_t *out)
{
    char *end       = NULL;
    uint64_t parsed = strtoull(text, &end, 0);

    if (end == text || *end != '\0' || parsed > 0xFFFFUL) return false;

    *out = (uint16_t) parsed;
    return true;
}

static int
cmd_set(katherine_device_t *device, const kdac_args_t *args)
{
    // Eighteen values is the whole vector, which costs one latch rather than
    // eighteen and leaves nothing composed with what the readout held.
    if (args->value_count == KDAC_SET_ALL_ARGC) {
        katherine_tpx3_dacs_t dacs;
        memset(&dacs, 0, sizeof(dacs));

        for (int i = 0; i < KDAC_SET_ALL_ARGC; ++i) {
            if (!parse_value(args->values[i], &dacs.array[i])) {
                fprintf(stderr, "value %d (\"%s\") is not a DAC code\n", i, args->values[i]);
                return 1;
            }
        }

        const katherine_error_t res = katherine_tpx3_set_dacs(device, args->chip_index, &dacs);
        if (res) {
            fprintf(stderr, "cannot write the DACs: %s\n", katherine_strerror(res));
            return 1;
        }

        if (!args->json) printf("wrote all %d DACs of chip %u\n", KDAC_SET_ALL_ARGC,
            (unsigned) args->chip_index);

        return 0;
    }

    if (args->value_count != 2) {
        fprintf(stderr, "set takes a DAC and a value, or all %d values\n", KDAC_SET_ALL_ARGC);
        return 1;
    }

    katherine_tpx3_dac_t dac;
    if (!find_dac(args->values[0], &dac)) {
        fprintf(stderr, "no DAC called \"%s\"; try `kdac list`\n", args->values[0]);
        return 1;
    }

    uint16_t value;
    if (!parse_value(args->values[1], &value)) {
        fprintf(stderr, "\"%s\" is not a DAC code\n", args->values[1]);
        return 1;
    }

    // The library transmits an out-of-range code unchecked, so say so here
    // rather than let the chip take a value its register cannot hold.
    const uint16_t max = katherine_tpx3_dac_max(dac);
    if (value > max) {
        fprintf(stderr, "%s holds at most %u, not %u\n", katherine_tpx3_dac_name(dac), max, value);
        return 1;
    }

    const katherine_error_t res = katherine_tpx3_set_dac(device, args->chip_index, dac, value);
    if (res) {
        fprintf(stderr, "cannot write %s: %s\n", katherine_tpx3_dac_name(dac),
            katherine_strerror(res));
        return 1;
    }

    if (args->json) return 0;

    if (args->si) {
        katherine_dac_unit_t unit = KATHERINE_DAC_UNIT_VOLT;
        const double si           = katherine_tpx3_dac_to_si(dac, value, &unit);
        printf("%s = %u (%.6g %s) on chip %u\n", katherine_tpx3_dac_name(dac), value, si,
            unit_symbol(unit), (unsigned) args->chip_index);
    } else {
        printf("%s = %u on chip %u\n", katherine_tpx3_dac_name(dac), value,
            (unsigned) args->chip_index);
    }

    return 0;
}

static int
cmd_scan(katherine_device_t *device, const kdac_args_t *args)
{
    if (args->value_count != 1 && args->value_count != 3) {
        fprintf(stderr, "scan takes a DAC, optionally with a first and a last code\n");
        return 1;
    }

    katherine_tpx3_dac_t dac;
    if (!find_dac(args->values[0], &dac)) {
        fprintf(stderr, "no DAC called \"%s\"; try `kdac list`\n", args->values[0]);
        return 1;
    }

    const uint16_t max = katherine_tpx3_dac_max(dac);
    uint16_t first = 0, last = max;

    if (args->value_count == 3) {
        if (!parse_value(args->values[1], &first) || !parse_value(args->values[2], &last)) {
            fprintf(stderr, "a scan range is two DAC codes\n");
            return 1;
        }
        if (first > last || last > max) {
            fprintf(stderr, "%s scans between 0 and %u\n", katherine_tpx3_dac_name(dac), max);
            return 1;
        }
    }

    // A DAC's own monitor sits at its own index, the first eighteen monitors
    // being the eighteen DACs.
    const katherine_tpx3_dac_monitor_t monitor = (katherine_tpx3_dac_monitor_t) dac;

    if (args->json) printf("[");

    for (uint32_t code = first; code <= last; ++code) {
        katherine_error_t res = katherine_tpx3_set_dac(device, args->chip_index, dac, (uint16_t) code);
        if (res) {
            fprintf(stderr, "\ncannot write %s: %s\n", katherine_tpx3_dac_name(dac),
                katherine_strerror(res));
            return 1;
        }

        float voltage = 0.f;
        res           = katherine_tpx3_get_dac_monitor_voltage(device, args->chip_index, monitor, &voltage);
        if (res) {
            fprintf(stderr, "\ncannot read %s: %s\n", katherine_tpx3_dac_monitor_name(monitor),
                katherine_strerror(res));
            return 1;
        }

        katherine_dac_unit_t unit = KATHERINE_DAC_UNIT_VOLT;
        const double si           = katherine_tpx3_dac_to_si(dac, (uint16_t) code, &unit);

        if (args->json) {
            printf("%s\n  {\"code\":%u,\"setpoint_si\":%.6g,\"unit\":\"%s\",\"volt\":%.6g}",
                code == first ? "" : ",", code, si, unit_symbol(unit), (double) voltage);
        } else if (args->si) {
            printf("%4u  %12.6g %s  %.4f V\n", code, si, unit_symbol(unit), (double) voltage);
        } else {
            printf("%4u  %.4f V\n", code, (double) voltage);
        }
    }

    if (args->json) printf("\n]\n");

    return 0;
}

// ------------------------------------------------------------------

int
main(int argc, char *argv[])
{
    if (kargs_wants_help(argc, argv)) {
        usage(stdout, argv[0]);
        return 0;
    }

    kdac_args_t args;

    if (!parse_args(argc, argv, &args)) {
        usage(stderr, argv[0]);
        return 2;
    }

    if (args.command == KDAC_LIST) return cmd_list(&args);

    if (args.address == NULL) {
        fprintf(stderr, "%s needs --address for this command\n", argv[0]);
        return 2;
    }

    katherine_device_t device;
    const katherine_error_t res = katherine_device_init(&device, args.address, 0);
    if (res) {
        fprintf(stderr, "cannot reach the readout at %s: %s\n", args.address,
            katherine_strerror(res));
        return 1;
    }

    // Opening succeeds whether or not the readout answered: the enumeration
    // inside katherine_device_init() is best-effort. Saying so here beats
    // letting the first per-chip call report a state error instead.
    if (!device.derived_info.supported) {
        fprintf(stderr, "no supported readout answered at %s\n", args.address);
        katherine_device_fini(&device);
        return 1;
    }

    int status = 0;
    switch (args.command) {
    case KDAC_MONITOR: status = cmd_monitor(&device, &args); break;
    case KDAC_SET:     status = cmd_set(&device, &args); break;
    case KDAC_SCAN:    status = cmd_scan(&device, &args); break;
    default:           status = 2; break;
    }

    katherine_device_fini(&device);

    return status;
}
