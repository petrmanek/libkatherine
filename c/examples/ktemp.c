/**
 * @file
 * @brief Example: periodically monitor readout and chip temperature.
 * @author Petr Mánek
 * @date 21.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

// Must precede every include: nanosleep() below is guarded by _POSIX_C_SOURCE,
// which only has an effect before the first libc header is pulled in. Harmless
// on Windows, whose headers do not gate on it.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdbool.h> // bool
#include <stdio.h>   // printf, fprintf, fflush
#include <stdlib.h>  // strtod, exit
#include <string.h>  // strcmp, strerror

// katherine/katherine.h must precede <windows.h> below: it transitively
// pulls in <winsock2.h> (via udp_win.h), and windows.h internally drags in
// the legacy Winsock 1.1 header unless Winsock 2 was already established
// first, so the reverse order does not compile under the Windows SDK.
#include <katherine/katherine.h>

#ifdef _WIN32
#include <windows.h> // Sleep
#else
#include <time.h> // nanosleep
#endif

typedef struct ktemp_args {
    bool show_readout;
    bool show_chip;
    double period; // seconds
    const char *address;
} ktemp_args_t;

static bool
parse_args(int argc, char *argv[], ktemp_args_t *args)
{
    args->show_readout = false;
    args->show_chip    = false;
    args->period       = 1.0;
    args->address      = NULL;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--readout") == 0) {
            args->show_readout = true;
        } else if (strcmp(arg, "--chip") == 0) {
            args->show_chip = true;
        } else if (strcmp(arg, "--period") == 0) {
            if (++i >= argc) return false;

            char *end     = NULL;
            double period = strtod(argv[i], &end);
            if (end == argv[i] || *end != '\0' || period <= 0) return false;

            args->period = period;
        } else if (args->address == NULL) {
            args->address = arg;
        } else {
            return false;
        }
    }

    if (args->address == NULL) return false;

    if (!args->show_readout && !args->show_chip) {
        args->show_readout = true;
        args->show_chip    = true;
    }

    return true;
}

// The C standard library has no portable sleep function, and this example
// otherwise only exercises the public libkatherine API, so a minimal helper
// is inlined here rather than added to the library.
static void
sleep_seconds(double seconds)
{
#ifdef _WIN32
    Sleep((DWORD) (seconds * 1e3));
#else
    struct timespec ts;
    ts.tv_sec  = (time_t) seconds;
    ts.tv_nsec = (long) ((seconds - (double) ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
#endif
}

static bool
poll_temperatures(katherine_device_t *device, const ktemp_args_t *args, int *err)
{
    float readout_temp = 0.f, chip_temp = 0.f;

    if (args->show_readout) {
        *err = katherine_get_readout_temperature(device, &readout_temp);
        if (*err != 0) return false;
    }

    if (args->show_chip) {
        *err = katherine_get_sensor_temperature(device, &chip_temp);
        if (*err != 0) return false;
    }

    if (args->show_readout && args->show_chip) {
        printf("readout: %.1f C, chip: %.1f C\n", readout_temp, chip_temp);
    } else if (args->show_readout) {
        printf("readout: %.1f C\n", readout_temp);
    } else {
        printf("chip: %.1f C\n", chip_temp);
    }

    fflush(stdout);
    return true;
}

static void
fail(const char *address)
{
    fprintf(stderr, "ktemp: no response from %s\n", address);
    exit(1);
}

int
main(int argc, char *argv[])
{
    ktemp_args_t args;
    if (!parse_args(argc, argv, &args)) {
        fprintf(stderr, "Usage: ktemp [--readout] [--chip] [--period <seconds>] <address>\n");
        return 1;
    }

    katherine_device_t device;
    if (katherine_device_init(&device, args.address) != 0) {
        fail(args.address);
    }

    bool had_success = false;
    for (;;) {
        int err = 0;
        if (poll_temperatures(&device, &args, &err)) {
            had_success = true;
        } else if (!had_success) {
            katherine_device_fini(&device);
            fail(args.address);
        } else {
            printf("ktemp: no response from %s: %s\n", args.address, strerror(err));
            fflush(stdout);
        }

        sleep_seconds(args.period);
    }
}
