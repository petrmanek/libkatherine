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

#include <cstdio>
#include <iostream>
#include <string>

#include <katherinexx/katherinexx.hpp>

static bool
parse_args(int argc, char *argv[], std::string& address)
{
    bool have_address = false;

    for (int i = 1; i < argc; ++i) {
        if (!have_address) {
            address      = argv[i];
            have_address = true;
        } else {
            return false;
        }
    }

    return have_address;
}

static void
report_failure(const std::string& step, const std::string& text)
{
    std::cerr << "kinfo: " << step << " failed: " << text << std::endl;
}

static bool
print_info(katherine::device& device)
{
    std::string chip_id;
    try {
        chip_id = device.chip_id();
    } catch (const katherine::error& e) {
        report_failure("katherine_get_chip_id", e.what());
        return false;
    }

    katherine_readout_status_t readout_status;
    try {
        readout_status = device.readout_status();
    } catch (const katherine::error& e) {
        report_failure("katherine_get_readout_status", e.what());
        return false;
    }

    katherine_comm_status_t comm_status;
    try {
        comm_status = device.comm_status();
    } catch (const katherine::error& e) {
        report_failure("katherine_get_comm_status", e.what());
        return false;
    }

    std::printf("%-15s: %s\n", "Chip ID", chip_id.c_str());
    std::printf("\n");
    std::printf("Readout status\n");
    std::printf("  %-13s: %d\n", "HW type", readout_status.hw_type);
    std::printf("  %-13s: %d\n", "HW revision", readout_status.hw_revision);
    std::printf("  %-13s: %d\n", "Serial number", readout_status.hw_serial_number);
    std::printf("  %-13s: %d\n", "FW version", readout_status.fw_version);
    std::printf("\n");
    std::printf("Comm status\n");
    std::printf("  %-13s: 0x%02x\n", "Comm lines", (unsigned) comm_status.comm_lines_mask);
    std::printf("  %-13s: %u\n", "Data rate", (unsigned) comm_status.data_rate);
    std::printf("  %-13s: %s\n", "Chip detected", comm_status.chip_detected ? "yes" : "no");

    return true;
}

int
main(int argc, char *argv[])
{
    std::string address;
    if (!parse_args(argc, argv, address)) {
        std::cerr << "Usage: kinfo <address>" << std::endl;
        return 1;
    }

    try {
        katherine::device device{address};
        return print_info(device) ? 0 : 1;
    } catch (const katherine::error& e) {
        report_failure("katherine_device_init", e.what());
        return 1;
    }
}
