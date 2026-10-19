/**
 * \file
 * \brief Enough of libusb to drive the USB backend without a bus.
 *
 * The backend's ring -- what it submits, what it does with a transfer that
 * comes back empty, and what closing it waits for -- is reachable no other
 * way: it sits behind libusb, and libusb needs hardware. This header stands in
 * for the real one so that a test can decide when a transfer completes and
 * with what, which is the whole of the behaviour worth asserting.
 *
 * It shadows the real <libusb.h> through the include path, for one test
 * target only. Nothing else in the build sees it.
 *
 * \author Petr Mánek
 * \date 15.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

#define LIBUSB_CALL

enum libusb_error {
    LIBUSB_SUCCESS             = 0,
    LIBUSB_ERROR_IO            = -1,
    LIBUSB_ERROR_INVALID_PARAM = -2,
    LIBUSB_ERROR_ACCESS        = -3,
    LIBUSB_ERROR_NO_DEVICE     = -4,
    LIBUSB_ERROR_NOT_FOUND     = -5,
    LIBUSB_ERROR_BUSY          = -6,
    LIBUSB_ERROR_TIMEOUT       = -7,
    LIBUSB_ERROR_NO_MEM        = -11,
    LIBUSB_ERROR_OTHER         = -99,
};

enum libusb_transfer_status {
    LIBUSB_TRANSFER_COMPLETED = 0,
    LIBUSB_TRANSFER_ERROR,
    LIBUSB_TRANSFER_TIMED_OUT,
    LIBUSB_TRANSFER_CANCELLED,
    LIBUSB_TRANSFER_STALL,
    LIBUSB_TRANSFER_NO_DEVICE,
    LIBUSB_TRANSFER_OVERFLOW,
};

typedef struct libusb_context libusb_context;
typedef struct libusb_device_handle libusb_device_handle;
struct libusb_transfer;

typedef void(LIBUSB_CALL *libusb_transfer_cb_fn)(struct libusb_transfer *transfer);

struct libusb_transfer {
    libusb_device_handle *dev_handle;
    unsigned char endpoint;
    unsigned int timeout;
    enum libusb_transfer_status status;
    int length;
    int actual_length;
    libusb_transfer_cb_fn callback;
    void *user_data;
    unsigned char *buffer;
};

struct libusb_init_option;

int
libusb_init_context(libusb_context **ctx, const struct libusb_init_option *options, int num);
void
libusb_exit(libusb_context *ctx);

libusb_device_handle *
libusb_open_device_with_vid_pid(
    libusb_context *ctx, uint16_t vendor_id, uint16_t product_id);
void
libusb_close(libusb_device_handle *handle);
int
libusb_claim_interface(libusb_device_handle *handle, int interface_number);
int
libusb_release_interface(libusb_device_handle *handle, int interface_number);
int
libusb_set_auto_detach_kernel_driver(libusb_device_handle *handle, int enable);

struct libusb_transfer *
libusb_alloc_transfer(int iso_packets);
void
libusb_free_transfer(struct libusb_transfer *transfer);
int
libusb_submit_transfer(struct libusb_transfer *transfer);
int
libusb_cancel_transfer(struct libusb_transfer *transfer);

int
libusb_handle_events_timeout_completed(
    libusb_context *ctx, struct timeval *tv, int *completed);

int
libusb_bulk_transfer(libusb_device_handle *handle, unsigned char endpoint,
    unsigned char *data, int length, int *transferred, unsigned int timeout);

static inline void
libusb_fill_bulk_transfer(struct libusb_transfer *transfer, libusb_device_handle *dev_handle,
    unsigned char endpoint, unsigned char *buffer, int length, libusb_transfer_cb_fn callback,
    void *user_data, unsigned int timeout)
{
    transfer->dev_handle = dev_handle;
    transfer->endpoint   = endpoint;
    transfer->buffer     = buffer;
    transfer->length     = length;
    transfer->callback   = callback;
    transfer->user_data  = user_data;
    transfer->timeout    = timeout;
}

// ------------------------------------------------------------------
// What the test drives. Not part of libusb.

/** Forget every transfer and every setting; call between cases. */
void
fake_usb_reset(void);

/** Whether a device with the asked-for identifiers can be opened. */
void
fake_usb_set_present(int present);

/** Make the next submission fail with this code, then go back to succeeding. */
void
fake_usb_fail_next_submit(int code);

/** How many transfers are submitted and not yet completed or cancelled. */
int
fake_usb_submitted(void);

/** How many submissions have ever been made. */
int
fake_usb_submit_count(void);

/**
 * Arrange for the oldest outstanding transfer to come back with this status
 * and length when events are next handled. Returns 0, or -1 if none is
 * outstanding.
 */
int
fake_usb_complete_oldest(enum libusb_transfer_status status, int actual_length);

/** How many synchronous bulk transfers have been made, and on which endpoint. */
int
fake_usb_bulk_count(void);
unsigned char
fake_usb_last_bulk_endpoint(void);

/** What the next synchronous bulk transfer returns. */
void
fake_usb_set_bulk_result(int code, int transferred);
