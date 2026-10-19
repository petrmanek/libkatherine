/**
 * \file
 * \brief The stand-in bus behind fake_libusb/libusb.h.
 * \author Petr Mánek
 * \date 15.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>

#include "libusb.h"

#define FAKE_MAX_TRANSFERS 256

struct pending {
    struct libusb_transfer *transfer;
    enum libusb_transfer_status status;
    int actual_length;
    int armed; ///< Set once the test has said how this one comes back.
};

static struct pending g_outstanding[FAKE_MAX_TRANSFERS];
static int g_outstanding_count;
static int g_submit_count;
static int g_present = 1;
static int g_fail_next_submit;
static int g_bulk_count;
static unsigned char g_last_bulk_endpoint;
static int g_bulk_code;
static int g_bulk_transferred;

// Addresses handed out as opaque handles; never dereferenced.
static int g_context_token;
static int g_handle_token;

void
fake_usb_reset(void)
{
    memset(g_outstanding, 0, sizeof(g_outstanding));
    g_outstanding_count  = 0;
    g_submit_count       = 0;
    g_present            = 1;
    g_fail_next_submit   = 0;
    g_bulk_count         = 0;
    g_last_bulk_endpoint = 0;
    g_bulk_code          = LIBUSB_SUCCESS;
    g_bulk_transferred   = 0;
}

void
fake_usb_set_present(int present)
{
    g_present = present;
}

void
fake_usb_fail_next_submit(int code)
{
    g_fail_next_submit = code;
}

int
fake_usb_submitted(void)
{
    return g_outstanding_count;
}

int
fake_usb_submit_count(void)
{
    return g_submit_count;
}

int
fake_usb_complete_oldest(enum libusb_transfer_status status, int actual_length)
{
    for (int i = 0; i < g_outstanding_count; ++i) {
        if (g_outstanding[i].armed) continue;

        g_outstanding[i].status        = status;
        g_outstanding[i].actual_length = actual_length;
        g_outstanding[i].armed         = 1;
        return 0;
    }

    return -1;
}

int
fake_usb_bulk_count(void)
{
    return g_bulk_count;
}

unsigned char
fake_usb_last_bulk_endpoint(void)
{
    return g_last_bulk_endpoint;
}

void
fake_usb_set_bulk_result(int code, int transferred)
{
    g_bulk_code        = code;
    g_bulk_transferred = transferred;
}

// ------------------------------------------------------------------

int
libusb_init_context(libusb_context **ctx, const struct libusb_init_option *options, int num)
{
    (void) options;
    (void) num;
    *ctx = (libusb_context *) &g_context_token;
    return LIBUSB_SUCCESS;
}

void
libusb_exit(libusb_context *ctx)
{
    (void) ctx;
}

libusb_device_handle *
libusb_open_device_with_vid_pid(libusb_context *ctx, uint16_t vendor_id, uint16_t product_id)
{
    (void) ctx;
    (void) vendor_id;
    (void) product_id;
    return g_present ? (libusb_device_handle *) &g_handle_token : NULL;
}

void
libusb_close(libusb_device_handle *handle)
{
    (void) handle;
}

int
libusb_claim_interface(libusb_device_handle *handle, int interface_number)
{
    (void) handle;
    (void) interface_number;
    return LIBUSB_SUCCESS;
}

int
libusb_release_interface(libusb_device_handle *handle, int interface_number)
{
    (void) handle;
    (void) interface_number;
    return LIBUSB_SUCCESS;
}

int
libusb_set_auto_detach_kernel_driver(libusb_device_handle *handle, int enable)
{
    (void) handle;
    (void) enable;
    return LIBUSB_SUCCESS;
}

struct libusb_transfer *
libusb_alloc_transfer(int iso_packets)
{
    (void) iso_packets;
    return calloc(1, sizeof(struct libusb_transfer));
}

void
libusb_free_transfer(struct libusb_transfer *transfer)
{
    free(transfer);
}

int
libusb_submit_transfer(struct libusb_transfer *transfer)
{
    if (g_fail_next_submit != 0) {
        const int code     = g_fail_next_submit;
        g_fail_next_submit = 0;
        return code;
    }

    if (g_outstanding_count == FAKE_MAX_TRANSFERS) return LIBUSB_ERROR_NO_MEM;

    g_outstanding[g_outstanding_count].transfer      = transfer;
    g_outstanding[g_outstanding_count].armed         = 0;
    g_outstanding[g_outstanding_count].status        = LIBUSB_TRANSFER_COMPLETED;
    g_outstanding[g_outstanding_count].actual_length = 0;
    g_outstanding_count += 1;
    g_submit_count += 1;

    return LIBUSB_SUCCESS;
}

// Cancellation is a request in libusb too: the transfer still comes back, with
// LIBUSB_TRANSFER_CANCELLED, the next time events are handled.
int
libusb_cancel_transfer(struct libusb_transfer *transfer)
{
    for (int i = 0; i < g_outstanding_count; ++i) {
        if (g_outstanding[i].transfer != transfer) continue;
        if (g_outstanding[i].armed) return LIBUSB_SUCCESS;

        g_outstanding[i].status        = LIBUSB_TRANSFER_CANCELLED;
        g_outstanding[i].actual_length = 0;
        g_outstanding[i].armed         = 1;
        return LIBUSB_SUCCESS;
    }

    return LIBUSB_ERROR_NOT_FOUND;
}

int
libusb_handle_events_timeout_completed(libusb_context *ctx, struct timeval *tv, int *completed)
{
    (void) ctx;
    (void) tv;
    (void) completed;

    // Every armed transfer is delivered, oldest first. The callback may submit
    // more, which land past the end and are not delivered by this pass -- the
    // same single-pass behaviour the real one has.
    const int delivered_through = g_outstanding_count;

    for (int i = 0; i < delivered_through; ++i) {
        if (!g_outstanding[i].armed) continue;

        struct libusb_transfer *transfer = g_outstanding[i].transfer;
        transfer->status                 = g_outstanding[i].status;
        transfer->actual_length          = g_outstanding[i].actual_length;

        // Removed before the callback runs, so that a callback which submits
        // sees a consistent bus.
        memmove(&g_outstanding[i], &g_outstanding[i + 1],
            (size_t) (g_outstanding_count - i - 1) * sizeof(g_outstanding[0]));
        g_outstanding_count -= 1;

        transfer->callback(transfer);
        return LIBUSB_SUCCESS;
    }

    return LIBUSB_SUCCESS;
}

int
libusb_bulk_transfer(libusb_device_handle *handle, unsigned char endpoint, unsigned char *data,
    int length, int *transferred, unsigned int timeout)
{
    (void) handle;
    (void) data;
    (void) length;
    (void) timeout;

    g_bulk_count += 1;
    g_last_bulk_endpoint = endpoint;
    if (transferred != NULL) *transferred = g_bulk_transferred;

    return g_bulk_code;
}
