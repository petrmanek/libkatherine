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

    const katherine_device_derived_info_t& derived = device.derived_info();

    std::printf("Device information\n");
    std::printf("  %-16s: %s\n", "Name", derived.name != nullptr ? derived.name : "(unknown)");
    std::printf("  %-16s: %s\n", "Chip type", katherine_str_chip_type(derived.chip_type));
    std::printf("  %-16s: %u\n", "Generation", (unsigned) derived.gen);
    std::printf("  %-16s: %u\n", "Max chips", (unsigned) derived.max_chip_count);
    std::printf("  %-16s: %u\n", "Bias supplies", (unsigned) derived.bias_supply_count);
    std::printf("  %-16s: %u\n", "Total GPIO", (unsigned) derived.all_gpio_count);
    std::printf("  %-16s: %u\n", "Accessible GPIO", (unsigned) derived.accessible_gpio_count);
    std::printf("  %-16s: %u\n", "TDC channels", (unsigned) derived.tdc_channel_count);
    std::printf("  %-16s: %u\n", "Pulse channels", (unsigned) derived.pulse_channel_count);
    std::printf("  %-16s: %s\n", "Supported", katherine_str_bool(derived.supported));
    std::printf("\n");
    std::printf("Readout status\n");
    std::printf("  %-16s: %d\n", "HW type", readout_status.hw_type);
    std::printf("  %-16s: %d\n", "HW revision", readout_status.hw_revision);
    std::printf("  %-16s: %d\n", "Serial number", readout_status.hw_serial_number);
    std::printf("  %-16s: %d\n", "FW version", readout_status.fw_version);
    std::printf("\n");
    std::printf("Comm status\n");
    std::printf("  %-16s: 0x%02x\n", "Comm lines", (unsigned) comm_status.comm_lines_mask);
    std::printf("  %-16s: %u Mb/s\n", "Data rate", (unsigned) comm_status.data_rate);
    std::printf("  %-16s: %u\n", "Chip count", (unsigned) comm_status.chip_count);
    std::printf("\n");

    // One line per chip the readout reports, rather than chip 0 and the
    // impression that a readout has one.
    std::printf("Detected chips\n");
    for (std::uint8_t chip = 0; chip < device.info().chip_count; ++chip) {
        try {
            std::printf("  Chip %-11u: %s\n", (unsigned) chip, device.chip_id(chip).c_str());
        } catch (const katherine::error& e) {
            report_failure("katherine_get_chip_id", e.what());
            return false;
        }
    }

    return true;
}

int
main(int argc, char *argv[])
{
    std::string address;
    if (!parse_args(argc, argv, address)) {
        std::cerr << "Usage: kinfo <address>" << std::endl;
        std::cerr << "  e.g. kinfo udp://192.168.1.42" << std::endl;
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
