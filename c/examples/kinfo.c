/**
 * @file
 * @brief Example: enumerate a readout: chip ID, readout and comm status.
 * @author Petr Mánek
 * @date 21.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
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

    err = katherine_get_chip_id(device, chip_id);
    if (err != 0) {
        report_failure("katherine_get_chip_id", err);
        return false;
    }

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

    printf("%-15s: %s\n", "Chip ID", chip_id);
    printf("\n");
    printf("Readout status\n");
    printf("  %-13s: %d\n", "HW type", readout_status.hw_type);
    printf("  %-13s: %d\n", "HW revision", readout_status.hw_revision);
    printf("  %-13s: %d\n", "Serial number", readout_status.hw_serial_number);
    printf("  %-13s: %d\n", "FW version", readout_status.fw_version);
    printf("\n");
    printf("Comm status\n");
    printf("  %-13s: 0x%02x\n", "Comm lines", (unsigned) comm_status.comm_lines_mask);
    printf("  %-13s: %u\n", "Data rate", (unsigned) comm_status.data_rate);
    printf("  %-13s: %s\n", "Chip detected", comm_status.chip_detected ? "yes" : "no");

    return true;
}

int
main(int argc, char *argv[])
{
    kinfo_args_t args;
    if (!parse_args(argc, argv, &args)) {
        fprintf(stderr, "Usage: kinfo <address>\n");
        return 1;
    }

    katherine_device_t device;
    int err = katherine_device_init(&device, args.address);
    if (err != 0) {
        report_failure("katherine_device_init", err);
        return 1;
    }

    bool ok = print_info(&device);
    katherine_device_fini(&device);

    return ok ? 0 : 1;
}
