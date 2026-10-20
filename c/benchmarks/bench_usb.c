/**
 * \file
 * \brief USB streaming benchmark: what the read discipline is worth (B3).
 *
 * A bulk IN endpoint carries data only while the host has a read outstanding.
 * This measures what that costs, by streaming the same endpoint twice: once
 * with one read at a time, and once with a ring of them refilled as buffers
 * come back. Decoding is stood in for by a configurable per-hit cost, so the
 * crossover can be found rather than assumed.
 *
 * With no arguments it lists what is on the bus and stops, which is how the
 * readout's vendor, product and endpoint addresses are found in the first
 * place.
 *
 * \author Petr Mánek
 * \date 14.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <katherine/katherine.h>

#ifndef KATHERINE_FEATURE_USB

int
main(void)
{
    fprintf(stderr, "built without the USB transport; configure with -DKATHERINE_BUILD_USB=ON\n");
    return 77;
}

#else

#include <libusb.h>

#include "transport/usb.h"
#include "monoclock.h"

static void
burn(uint64_t ns)
{
    if (ns == 0) return;
    const uint64_t end = katherine_clock_monotonic_ns() + ns;
    while (katherine_clock_monotonic_ns() < end) { }
}

// Everything on the bus, with the bulk endpoints of every interface, so that
// a readout's addresses can be read off rather than guessed.
static int
list_bus(void)
{
    libusb_context *ctx = NULL;
    if (libusb_init_context(&ctx, NULL, 0) != LIBUSB_SUCCESS) {
        fprintf(stderr, "cannot start a libusb session\n");
        return 1;
    }

    libusb_device **list = NULL;
    const ssize_t count  = libusb_get_device_list(ctx, &list);

    for (ssize_t i = 0; i < count; ++i) {
        struct libusb_device_descriptor dd;
        if (libusb_get_device_descriptor(list[i], &dd) != LIBUSB_SUCCESS) continue;

        printf("%04x:%04x  bus %u addr %u",
            dd.idVendor, dd.idProduct,
            libusb_get_bus_number(list[i]), libusb_get_device_address(list[i]));

        const int speed                   = libusb_get_device_speed(list[i]);
        static const char *const speeds[] = {
            "unknown", "low", "full", "high", "super", "super+"};
        if (speed >= 0 && (size_t) speed < sizeof(speeds) / sizeof(speeds[0]))
            printf("  %s-speed", speeds[speed]);
        printf("\n");

        struct libusb_config_descriptor *cfg = NULL;
        if (libusb_get_active_config_descriptor(list[i], &cfg) != LIBUSB_SUCCESS) continue;

        for (uint8_t in = 0; in < cfg->bNumInterfaces; ++in) {
            for (int alt = 0; alt < cfg->interface[in].num_altsetting; ++alt) {
                const struct libusb_interface_descriptor *id = &cfg->interface[in].altsetting[alt];
                for (uint8_t e = 0; e < id->bNumEndpoints; ++e) {
                    const struct libusb_endpoint_descriptor *ep = &id->endpoint[e];
                    if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK)
                        continue;
                    printf("    interface %u alt %d  bulk %s  ep 0x%02x  max packet %u\n",
                        in, alt,
                        (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) ? "IN " : "OUT",
                        ep->bEndpointAddress, ep->wMaxPacketSize);
                }
            }
        }

        libusb_free_config_descriptor(cfg);
    }

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);

    return 0;
}

struct outcome {
    double mb_per_s;
    double hits_per_s;
    uint64_t buffers;
    uint64_t bytes;
    uint64_t timeouts;
};

static struct outcome
stream(const katherine_usb_config_t *config, uint64_t per_hit_ns, double seconds)
{
    struct outcome o;
    memset(&o, 0, sizeof(o));

    // Zeroed before the call: a failed open leaves it untouched, and the
    // accessors below have to see an empty transport rather than whatever was
    // on the stack.
    katherine_transport_t t;
    memset(&t, 0, sizeof(t));

    const katherine_error_t res = katherine_transport_init_usb(&t, config);
    if (res == KATHERINE_E_ADDRESS) {
        // The library's word for this is "address resolution or bind failed",
        // which is the network's language rather than the bus's.
        fprintf(stderr, "no readout at that address; run with no arguments to list what is on the bus\n");
        exit(1);
    }
    if (res) {
        fprintf(stderr, "cannot open the readout over USB: %s\n", katherine_strerror(res));
        exit(1);
    }

    const uint64_t t0    = katherine_clock_monotonic_ns();
    const uint64_t t_end = t0 + (uint64_t) (seconds * 1e9);

    while (katherine_clock_monotonic_ns() < t_end) {
        void *buf  = NULL;
        size_t len = 0;

        const katherine_error_t r = katherine_transport_acquire_data_buf(&t, &buf, &len);
        if (r == KATHERINE_E_TIMEOUT) {
            o.timeouts += 1;
            continue;
        }
        if (r) {
            // The library's codes are deliberately coarse -- several libusb
            // failures map onto KATHERINE_E_IO -- so the underlying one is
            // printed beside it where there is one. On a bench that is the
            // difference between "permissions" and "wrong endpoint", which
            // otherwise read alike.
            const int code = katherine_transport_last_os_error(&t);
            if (code != 0) {
                fprintf(stderr, "stream ended: %s (libusb: %s)\n", katherine_strerror(r),
                    libusb_strerror(code));
            } else {
                fprintf(stderr, "stream ended: %s\n", katherine_strerror(r));
            }
            break;
        }

        o.bytes += len;
        o.buffers += 1;

        // Stands in for decoding what was just lent.
        burn(per_hit_ns * (len / KATHERINE_MD_SIZE));

        (void) katherine_transport_release_data_buf(&t, buf);
    }

    const double elapsed = (double) (katherine_clock_monotonic_ns() - t0) / 1e9;
    katherine_transport_fini(&t);

    o.mb_per_s   = (double) o.bytes / elapsed / 1e6;
    o.hits_per_s = (double) o.bytes / KATHERINE_MD_SIZE / elapsed;

    return o;
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s <address> [seconds] [ns_per_hit...]\n", argv[0]);
        printf("       %s  (lists the bus)\n\n", argv[0]);
        printf("       address is \"usb://auto\" for the one readout attached, or a\n");
        printf("       socket such as \"usb://4-1\" as the listing prints it\n\n");
        return list_bus();
    }

    katherine_usb_config_t config = katherine_usb_config_default();
    if (katherine_usb_config_set_address(&config, argv[1]) != KATHERINE_E_OK) {
        fprintf(stderr, "not a USB address: %s\n", argv[1]);
        return 1;
    }

    const double seconds = argc > 2 ? atof(argv[2]) : 3.0;

    uint64_t per_hit[8]  = {0, 5, 10, 18, 0, 0, 0, 0};
    size_t per_hit_count = 4;
    if (argc > 3) {
        per_hit_count = 0;
        for (int i = 3; i < argc && per_hit_count < 8; ++i)
            per_hit[per_hit_count++] = strtoull(argv[i], NULL, 0);
    }

    printf("# %s, %u reads of %u B, %.1f s per point\n",
        argv[1], config.queue_len, config.buffer_size, seconds);

    for (size_t i = 0; i < per_hit_count; ++i) {
        for (int shape = 0; shape < 2; ++shape) {
            config.stream = shape == 0 ? KATHERINE_USB_STREAM_SYNC : KATHERINE_USB_STREAM_ASYNC;

            const struct outcome o = stream(&config, per_hit[i], seconds);

            printf("{\"bench\":\"usb_%s_%" PRIu64 "ns\",\"value\":%.3f,\"unit\":\"Mhit/s\"}\n",
                katherine_str_usb_stream(config.stream), per_hit[i], o.hits_per_s / 1e6);
            printf("# %s %" PRIu64 " ns/hit: %.1f MB/s, %" PRIu64 " buffers, %" PRIu64 " timeouts\n",
                katherine_str_usb_stream(config.stream), per_hit[i],
                o.mb_per_s, o.buffers, o.timeouts);
        }
    }

    return 0;
}

#endif /* KATHERINE_FEATURE_USB */
