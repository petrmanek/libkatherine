/**
 * \file
 * \brief Example: periodically monitor readout and chip temperature.
 * \author Petr Mánek
 * \date 21.8.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

#include <katherinexx/katherinexx.hpp>

struct ktemp_args {
    bool show_readout;
    bool show_chip;
    double period; // seconds
    std::string address;
};

static bool
parse_args(int argc, char *argv[], ktemp_args& args)
{
    args.show_readout = false;
    args.show_chip    = false;
    args.period       = 1.0;

    bool have_address = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg{argv[i]};

        if (arg == "--readout") {
            args.show_readout = true;
        } else if (arg == "--chip") {
            args.show_chip = true;
        } else if (arg == "--period") {
            if (++i >= argc) return false;

            const std::string period_arg{argv[i]};
            std::size_t pos = 0;
            try {
                args.period = std::stod(period_arg, &pos);
            } catch (const std::exception&) {
                return false;
            }
            if (pos != period_arg.size() || args.period <= 0) return false;
        } else if (!have_address) {
            args.address = arg;
            have_address = true;
        } else {
            return false;
        }
    }

    if (!have_address) return false;

    if (!args.show_readout && !args.show_chip) {
        args.show_readout = true;
        args.show_chip    = true;
    }

    return true;
}

static bool
poll_temperatures(katherine::device& device, const ktemp_args& args, std::string& err)
{
    float readout_temp = 0.f, chip_temp = 0.f;

    try {
        if (args.show_readout) {
            readout_temp = device.readout_temperature();
        }

        if (args.show_chip) {
            chip_temp = device.sensor_temperature();
        }
    } catch (const katherine::error& e) {
        err = e.what();
        return false;
    }

    if (args.show_readout && args.show_chip) {
        std::printf("readout: %.1f C, chip: %.1f C\n", readout_temp, chip_temp);
    } else if (args.show_readout) {
        std::printf("readout: %.1f C\n", readout_temp);
    } else {
        std::printf("chip: %.1f C\n", chip_temp);
    }

    std::fflush(stdout);
    return true;
}

int
main(int argc, char *argv[])
{
    ktemp_args args;
    if (!parse_args(argc, argv, args)) {
        std::cerr << "Usage: ktemp [--readout] [--chip] [--period <seconds>] <address>" << std::endl;
        return 1;
    }

    try {
        katherine::device device{args.address};

        bool had_success = false;
        for (;;) {
            std::string err;
            if (poll_temperatures(device, args, err)) {
                had_success = true;
            } else if (!had_success) {
                std::cerr << "ktemp: no response from " << args.address << std::endl;
                return 1;
            } else {
                std::printf("ktemp: no response from %s: %s\n", args.address.c_str(), err.c_str());
                std::fflush(stdout);
            }

            std::this_thread::sleep_for(std::chrono::duration<double>(args.period));
        }
    } catch (const katherine::error&) {
        std::cerr << "ktemp: no response from " << args.address << std::endl;
        return 1;
    }

    return 0;
}
