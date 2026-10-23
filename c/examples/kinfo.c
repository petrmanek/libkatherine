/**
 * \file
 * \brief Example: enumerate a readout: chip ID, readout and comm status.
 * \author Petr Mánek
 * \date 21.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h> // bool
#include <stdio.h>   // printf, fprintf

#include <katherine/katherine.h>

typedef struct kinfo_args {
    const char *address;
} kinfo_args_t;

static bool
parse_args(int argc, char *argv[], kinfo_args_t *args)
{
    args->address = NULL;

    for (int i = 1; i < argc; ++i) {
        if (args->address == NULL) {
            args->address = argv[i];
        } else {
            return false;
        }
    }

    return args->address != NULL;
}

static void
report_failure(const char *step, int err)
{
    fprintf(stderr, "kinfo: %s failed: %s\n", step, katherine_strerror(err));
}

static bool
print_info(katherine_device_t *device)
{
    char chip_id[KATHERINE_CHIP_ID_STR_SIZE];
    katherine_readout_status_t readout_status;
    katherine_comm_status_t comm_status;
    int err;

    err = katherine_get_readout_status(device, &readout_status);
    if (err != 0) {
        report_failure("katherine_get_readout_status", err);
        return false;
    }

    err = katherine_get_comm_status(device, &comm_status);
    if (err != 0) {
        report_failure("katherine_get_comm_status", err);
        return false;
    }

    const katherine_device_derived_info_t *derived = &device->derived_info;

    printf("Device information\n");
    printf("  %-16s: %s\n", "Name", derived->name != NULL ? derived->name : "(unknown)");
    printf("  %-16s: %s\n", "Chip type", katherine_str_chip_type(derived->chip_type));
    printf("  %-16s: %u\n", "Generation", (unsigned) derived->gen);
    printf("  %-16s: %u\n", "Max chips", (unsigned) derived->max_chip_count);
    printf("  %-16s: %u\n", "Bias supplies", (unsigned) derived->bias_supply_count);
    printf("  %-16s: %u\n", "Total GPIO", (unsigned) derived->all_gpio_count);
    printf("  %-16s: %u\n", "Accessible GPIO", (unsigned) derived->accessible_gpio_count);
    printf("  %-16s: %u\n", "TDC channels", (unsigned) derived->tdc_channel_count);
    printf("  %-16s: %u\n", "Pulse channels", (unsigned) derived->pulse_channel_count);
    printf("  %-16s: %s\n", "Supported", katherine_str_bool(derived->supported));
    printf("\n");
    printf("Readout status\n");
    printf("  %-16s: %d\n", "HW type", readout_status.hw_type);
    printf("  %-16s: %d\n", "HW revision", readout_status.hw_revision);
    printf("  %-16s: %d\n", "Serial number", readout_status.hw_serial_number);
    printf("  %-16s: %d\n", "FW version", readout_status.fw_version);
    printf("\n");
    printf("Comm status\n");
    printf("  %-16s: 0x%02x\n", "Comm lines", (unsigned) comm_status.comm_lines_mask);
    printf("  %-16s: %u Mb/s\n", "Data rate", (unsigned) comm_status.data_rate);
    printf("  %-16s: %u\n", "Chip count", comm_status.chip_count);
    printf("\n");

    // One line per chip the readout reports, rather than chip 0 and the
    // impression that a readout has one. device->info.chip_count is what
    // every per-chip call is bounded by, so it is the bound here too.
    printf("Detected chips\n");
    for (uint8_t chip = 0; chip < device->info.chip_count; ++chip) {
        err = katherine_get_chip_id(device, chip, chip_id);
        if (err != 0) {
            report_failure("katherine_get_chip_id", err);
            return false;
        }

        printf("  Chip %-11u: %s\n", (unsigned) chip, chip_id);
    }

    return true;
}

int
main(int argc, char *argv[])
{
    kinfo_args_t args;
    if (!parse_args(argc, argv, &args)) {
        fprintf(stderr, "Usage: kinfo <address>\n");
        fprintf(stderr, "  e.g. kinfo udp://192.168.1.42\n");
        return 1;
    }

    katherine_device_t device;
    int err = katherine_device_init(&device, args.address, 0);
    if (err != 0) {
        report_failure("katherine_device_init", err);
        return 1;
    }

    bool ok = print_info(&device);
    katherine_device_fini(&device);

    return ok ? 0 : 1;
}
