/**
 * \file
 * \brief A readout on a USB bus, driven through the transport operations.
 * \author Petr Mánek
 * \date 14.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <katherine/global.h>

#ifdef KATHERINE_FEATURE_USB

// Before every other libc include here: monoclock.h sets _POSIX_C_SOURCE, and
// glibc resolves POSIX visibility once, at the first libc header of the
// translation unit. Included after <stdlib.h> instead, clock_gettime() is
// invisible under a strict standard -- which the standards leg of CI builds
// with, so this is a build failure there and nowhere else.
#include "monoclock.h"

#include <errno.h>
#include <libusb.h>
#include <stdlib.h>
#include <string.h>

#include <katherine/usb.h>
#include "address.h"
#include "mutex.h"
#include "msleep.h"
#include "protocol/cmd_interface.h"

// How many event-handling passes closing waits through before saying the
// cancellation again and settling in to wait it out. At 20 ms a pass this is
// one second, which is what usb-skeleton.c gives its anchored URBs before it
// escalates to usb_kill_anchored_urbs().
#define DRAIN_POLITE_ATTEMPTS 50

// A lent buffer's place in its owner's ring.
enum slot_state {
    SLOT_IDLE = 0, ///< Not submitted, nothing in it.
    SLOT_INFLIGHT, ///< Submitted; libusb owns the buffer.
    SLOT_FILLED,   ///< Came back with data, waiting for a caller.
    SLOT_LENT,     ///< A caller is reading it.
};

struct slot {
    struct libusb_transfer *xfer;
    uint8_t *buf;
    int len;
    enum slot_state state;
    uint32_t index;
    struct katherine_usb *usb;
};

struct katherine_usb {
    libusb_context *ctx;
    libusb_device_handle *handle;
    katherine_usb_config_t config;

    // The completion callback can run on any thread that handles events on
    // this context -- including a thread outside this library, and including
    // the caller's own thread inside a synchronous command transfer, which
    // services this context too. Everything it touches is therefore under
    // this lock, and it does no more than record what came back.
    katherine_mutex_t lock;

    // What katherine_transport_mutex_lock() takes, and deliberately not the
    // lock above. That one is held for the few instructions a ring update
    // costs and is dropped again inside submit_slot(); this one is held
    // across a whole request-and-response exchange and across an entire read
    // loop. One mutex could not do both: the read loop would hold it while
    // the completion callback waited for it.
    katherine_mutex_t api_lock;

    struct slot *slots;
    uint32_t *ready; ///< Indices of filled slots, oldest first.
    uint32_t ready_head;
    uint32_t ready_count;
    uint32_t inflight;

    int last_os_error;
    enum libusb_transfer_status last_status;
    bool draining;
};

static katherine_error_t
map_usb_error(struct katherine_usb *u, int code)
{
    u->last_os_error = code;

    switch (code) {
    case LIBUSB_SUCCESS:             return KATHERINE_E_OK;
    case LIBUSB_ERROR_TIMEOUT:       return KATHERINE_E_TIMEOUT;
    case LIBUSB_ERROR_NO_DEVICE:     /* fall through */
    case LIBUSB_ERROR_NOT_FOUND:     return KATHERINE_E_CLOSED;
    case LIBUSB_ERROR_NO_MEM:        return KATHERINE_E_NOMEM;
    case LIBUSB_ERROR_INVALID_PARAM: return KATHERINE_E_INVAL;
    case LIBUSB_ERROR_ACCESS:        /* fall through */
    case LIBUSB_ERROR_BUSY:          /* fall through */
    default:                         return KATHERINE_E_IO;
    }
}

// Names the one readout attached rather than a socket: "usb://auto". Spelled
// here rather than in the header because a caller writes the address out, as
// it does "udp://192.168.1.42", and the scheme is rendered by
// katherine_str_transport_kind() rather than pasted together at compile time.
static const char USB_LOCATION_AUTO[] = "auto";

// What the readout is on the bus, and how its firmware plumbed the endpoints.
// Not a caller's to choose: a Katherine readout answers at these and nowhere
// else, so a configurable copy could only ever be set wrong.
//
// The numbering is not the order it suggests -- the lower IN endpoint carries
// measurement data and the higher one carries command responses.
static const uint16_t USB_VENDOR_ID  = 0x16D0;
static const uint16_t USB_PRODUCT_ID = 0x118A;

static const uint8_t USB_INTERFACE  = 0;
static const uint8_t USB_EP_CMD_OUT = 0x02;
static const uint8_t USB_EP_CMD_IN  = 0x82;
static const uint8_t USB_EP_DATA_IN = 0x81;

// Runs on whichever thread handled events on this context. It records the
// outcome and nothing else: no user code, and no resubmission, which happens
// on the consumer's thread in release_data_buf() once the buffer is free.
static void LIBUSB_CALL
on_transfer_done(struct libusb_transfer *xfer)
{
    struct slot *s          = (struct slot *) xfer->user_data;
    struct katherine_usb *u = s->usb;

    (void) katherine_mutex_lock(&u->lock);

    u->inflight -= 1;

    switch (xfer->status) {
    case LIBUSB_TRANSFER_COMPLETED:
    // A bulk read that timed out may still have brought back a short buffer,
    // and those bytes are measurement data like any other.
    case LIBUSB_TRANSFER_TIMED_OUT:
        s->len = xfer->actual_length;
        if (s->len > 0) {
            s->state                                                         = SLOT_FILLED;
            u->ready[(u->ready_head + u->ready_count) % u->config.queue_len] = s->index;
            u->ready_count += 1;
        } else {
            // Nothing came back, which an idle readout does once per timeout
            // on every read. The slot is reposted by acquire_data_buf() on
            // the consumer's thread rather than here, so that submission
            // stays off whichever thread happened to handle the event.
            s->state = SLOT_IDLE;
        }
        break;

    case LIBUSB_TRANSFER_CANCELLED:
        s->state = SLOT_IDLE;
        break;

    default:
        u->last_status = xfer->status;
        s->state       = SLOT_IDLE;
        break;
    }

    (void) katherine_mutex_unlock(&u->lock);
}

// Caller must hold the lock; it is dropped around the submission because
// libusb may complete the transfer, and so take the lock, before returning.
// \p held says whether it came back, since a caller must not release a lock
// it no longer holds.
static katherine_error_t
submit_slot(struct katherine_usb *u, struct slot *s, bool *held)
{
    katherine_error_t res;

    s->state = SLOT_INFLIGHT;
    u->inflight += 1;

    if ((res = katherine_mutex_unlock(&u->lock)) != KATHERINE_E_OK) goto err_unlock;
    *held = false;

    const int code = libusb_submit_transfer(s->xfer);

    if ((res = katherine_mutex_lock(&u->lock)) != KATHERINE_E_OK) goto err_relock;
    *held = true;

    if (code != LIBUSB_SUCCESS) {
        s->state = SLOT_IDLE;
        u->inflight -= 1;
        res = map_usb_error(u, code);
        goto err_submit;
    }

    return KATHERINE_E_OK;

err_unlock:
    // Nothing reached the bus, so the slot goes back as it was.
    s->state = SLOT_IDLE;
    u->inflight -= 1;
err_relock:
err_submit:
    return res;
}

static katherine_error_t
usb_send_cmd(void *ctx, const void *data, size_t count)
{
    katherine_error_t res;
    struct katherine_usb *u = ctx;
    int moved               = 0;

    const int code = libusb_bulk_transfer(u->handle, USB_EP_CMD_OUT, (unsigned char *) (uintptr_t) data, (int) count, &moved, u->config.timeout_ms);
    if (code != LIBUSB_SUCCESS) {
        res = map_usb_error(u, code);
        goto err_write;
    }

    // Short of what was asked for, with no error to say why. A command is one
    // datagram's worth either way, so a partial one is a failed one.
    if ((size_t) moved != count) {
        res = KATHERINE_E_IO;
        goto err_short;
    }

    return KATHERINE_E_OK;

err_short:
err_write:
    return res;
}

static katherine_error_t
usb_recv_crd(void *ctx, void *crd, size_t *len)
{
    katherine_error_t res;
    struct katherine_usb *u = ctx;
    int moved               = 0;

    const int code = libusb_bulk_transfer(u->handle, USB_EP_CMD_IN, crd, (int) *len, &moved, u->config.timeout_ms);
    if (code != LIBUSB_SUCCESS) {
        res = map_usb_error(u, code);
        goto err_read;
    }

    // What arrived, rather than what was asked for. A response longer than a
    // CRD is then seen to be longer instead of passing as a short read.
    *len = (size_t) moved;

    return KATHERINE_E_OK;

err_read:
    return res;
}

static katherine_error_t
usb_flush_incoming(void *ctx, uint32_t *discarded)
{
    struct katherine_usb *u = ctx;
    uint8_t scratch[512];

    // Bounded like udp_drain(), and for the same reason: a readout still
    // talking would otherwise keep this here indefinitely, and the operation
    // is contracted to return at once.
    for (*discarded = 0; *discarded < KATHERINE_CMD_MAX_DRAIN; ++*discarded) {
        int moved = 0;

        // One millisecond rather than none: libusb reads zero as "no
        // timeout", which is the opposite of what is wanted here.
        if (libusb_bulk_transfer(u->handle, USB_EP_CMD_IN, scratch, (int) sizeof(scratch), &moved, 1) != LIBUSB_SUCCESS || moved == 0) break;
    }

    return KATHERINE_E_OK;
}

// One read at a time, issued now. Nothing crosses the bus between the moment
// the previous buffer came back and the moment this call is made.
static katherine_error_t
usb_acquire_sync(void *ctx, void **buf, size_t *len)
{
    katherine_error_t res;
    struct katherine_usb *u = ctx;
    struct slot *s          = &u->slots[0];
    int moved               = 0;

    const int code = libusb_bulk_transfer(u->handle, USB_EP_DATA_IN, s->buf, (int) u->config.buffer_size, &moved, u->config.timeout_ms);

    // A timeout that still brought bytes back is data, not a failure.
    if (code != LIBUSB_SUCCESS && !(code == LIBUSB_ERROR_TIMEOUT && moved > 0)) {
        res = map_usb_error(u, code);
        goto err_read;
    }

    *buf = s->buf;
    *len = (size_t) moved;

    return KATHERINE_E_OK;

err_read:
    return res;
}

static katherine_error_t
usb_release_sync(void *ctx, void *buf)
{
    (void) ctx;
    (void) buf;

    return KATHERINE_E_OK;
}

// Several reads outstanding, so the bus keeps filling buffers while the
// caller decodes the one it was lent.
static katherine_error_t
usb_acquire_async(void *ctx, void **buf, size_t *len)
{
    katherine_error_t res;
    struct katherine_usb *u = ctx;
    const uint64_t deadline = katherine_clock_monotonic_ns() + (uint64_t) u->config.timeout_ms * 1000000ULL;
    bool held               = false;

    if ((res = katherine_mutex_lock(&u->lock)) != KATHERINE_E_OK) goto err_lock;
    held = true;

    for (;;) {
        // Repost whatever came back empty or failed. Without this the ring
        // drains away a slot at a time -- an idle readout times every read
        // out, so the whole of it would be gone within one timeout and the
        // stream would never resume.
        if (!u->draining) {
            for (uint32_t i = 0; i < u->config.queue_len; ++i) {
                if (u->slots[i].state != SLOT_IDLE) continue;

                // A readout that has gone away fails every repost, so saying
                // so beats waiting out the timeout to report silence.
                res = submit_slot(u, &u->slots[i], &held);
                if (res && u->ready_count == 0) goto err_submit;
            }
        }

        if (u->ready_count > 0) {
            const uint32_t i = u->ready[u->ready_head];
            u->ready_head    = (u->ready_head + 1) % u->config.queue_len;
            u->ready_count -= 1;

            struct slot *s = &u->slots[i];
            s->state       = SLOT_LENT;

            *buf = s->buf;
            *len = (size_t) s->len;

            if ((res = katherine_mutex_unlock(&u->lock)) != KATHERINE_E_OK) goto err_lent_unlock;
            held = false;

            return KATHERINE_E_OK;
        }

        if (u->inflight == 0) {
            res = KATHERINE_E_STATE;
            goto err_stalled;
        }

        // The lock is dropped across event handling: the completion callback
        // takes it, so holding it here would deadlock against our own pump.
        if ((res = katherine_mutex_unlock(&u->lock)) != KATHERINE_E_OK) goto err_pump_unlock;
        held = false;

        struct timeval tv = {.tv_sec = 0, .tv_usec = 20000};
        int completed     = 0;
        const int code    = libusb_handle_events_timeout_completed(u->ctx, &tv, &completed);

        if ((res = katherine_mutex_lock(&u->lock)) != KATHERINE_E_OK) goto err_pump_relock;
        held = true;

        if (code != LIBUSB_SUCCESS) {
            res = map_usb_error(u, code);
            goto err_events;
        }

        if (katherine_clock_monotonic_ns() >= deadline && u->ready_count == 0) {
            res = KATHERINE_E_TIMEOUT;
            goto err_timeout;
        }
    }

err_timeout:
err_events:
err_stalled:
err_submit:
err_lent_unlock:
err_pump_unlock:
    // An error is already on its way out, so a failure to release cannot be
    // reported on top of it -- as everywhere else in the library.
    if (held) (void) katherine_mutex_unlock(&u->lock);
err_pump_relock:
err_lock:
    return res;
}

static katherine_error_t
usb_release_async(void *ctx, void *buf)
{
    katherine_error_t res;
    struct katherine_usb *u = ctx;
    bool held               = false;

    if ((res = katherine_mutex_lock(&u->lock)) != KATHERINE_E_OK) goto err_lock;
    held = true;

    struct slot *s = NULL;
    for (uint32_t i = 0; i < u->config.queue_len; ++i) {
        if (u->slots[i].buf == (unsigned char *) buf) {
            s = &u->slots[i];
            break;
        }
    }

    if (s == NULL || s->state != SLOT_LENT) {
        res = KATHERINE_E_INVAL;
        goto err_foreign;
    }

    if (u->draining) {
        // Closing down, so the slot goes idle rather than back onto the bus:
        // the ring is about to be taken apart.
        s->state = SLOT_IDLE;
    } else {
        res = submit_slot(u, s, &held);
        if (res) goto err_submit;
    }

    if ((res = katherine_mutex_unlock(&u->lock)) != KATHERINE_E_OK) goto err_unlock;
    held = false;

    return KATHERINE_E_OK;

err_submit:
err_foreign:
    // See usb_acquire_async(): an error is already on its way out.
    if (held) (void) katherine_mutex_unlock(&u->lock);
err_unlock:
err_lock:
    return res;
}

static katherine_error_t
usb_set_timeout(void *ctx, uint32_t timeout_ms)
{
    katherine_error_t res;
    struct katherine_usb *u = ctx;

    if ((res = katherine_mutex_lock(&u->lock)) != KATHERINE_E_OK) goto err_lock;

    u->config.timeout_ms = timeout_ms;
    for (uint32_t i = 0; i < u->config.queue_len; ++i)
        if (u->slots[i].xfer != NULL) u->slots[i].xfer->timeout = timeout_ms;

    if ((res = katherine_mutex_unlock(&u->lock)) != KATHERINE_E_OK) goto err_unlock;

    return KATHERINE_E_OK;

err_unlock:
err_lock:
    return res;
}

// Taken across a whole exchange or a whole read loop, which is why it is not
// the ring lock: that one is dropped inside submit_slot() and taken by the
// completion callback, so a reader holding it for the length of a loop would
// stop the very callbacks the loop is waiting for.
static katherine_error_t
usb_mutex_lock(void *ctx)
{
    katherine_error_t res;
    struct katherine_usb *u = ctx;

    if ((res = katherine_mutex_lock(&u->api_lock)) != KATHERINE_E_OK) goto err_lock;

    return KATHERINE_E_OK;

err_lock:
    return res;
}

static katherine_error_t
usb_mutex_unlock(void *ctx)
{
    katherine_error_t res;
    struct katherine_usb *u = ctx;

    if ((res = katherine_mutex_unlock(&u->api_lock)) != KATHERINE_E_OK) goto err_unlock;

    return KATHERINE_E_OK;

err_unlock:
    return res;
}

static int
usb_last_os_error(void *ctx)
{
    struct katherine_usb *u = ctx;

    return u->last_os_error;
}

// Transports a bus would not release, kept reachable on purpose.
//
// The memory is not recoverable -- libusb still owns the buffers and will
// write into them if the device ever answers -- but unreachable and
// unaccounted are different things. On this list a leak checker sees it as
// live rather than lost, so the deliberate decision does not read as a bug in
// the sanitizer legs; and a core file shows how many readouts stopped
// answering, which is the question being asked when someone opens one.
// Cancelling is a request, not an act: every outstanding transfer still calls
// back, so the ring is only safe to free once none is left in flight. Caller
// holds the lock.
static void
cancel_outstanding(struct katherine_usb *u)
{
    for (uint32_t i = 0; i < u->config.queue_len; ++i)
        if (u->slots[i].state == SLOT_INFLIGHT)
            (void) libusb_cancel_transfer(u->slots[i].xfer);
}

// Hands events to libusb until nothing is outstanding, or -- where \p limit
// is nonzero -- until that many passes have gone by.
//
// A pass that fails is waited out rather than obeyed: the transfers are still
// the bus's either way, so there is nothing to do but try again, and sleeping
// first keeps a context libusb cannot pump from spinning a core.
//
// Caller holds the lock; it is dropped around each pass, because the
// completion callback takes it. Every iteration re-takes it before testing
// again, including the one that leaves: going by any other route would unlock
// a mutex this no longer holds.
static void
pump_until_quiet(struct katherine_usb *u, uint32_t limit)
{
    for (uint32_t attempt = 0; u->inflight > 0; ++attempt) {
        if (limit != 0 && attempt >= limit) break;

        (void) katherine_mutex_unlock(&u->lock);

        struct timeval tv = {.tv_sec = 0, .tv_usec = 20000};
        int completed     = 0;
        const int code    = libusb_handle_events_timeout_completed(u->ctx, &tv, &completed);
        if (code != LIBUSB_SUCCESS) katherine_msleep(20);

        (void) katherine_mutex_lock(&u->lock);
    }
}

// Gives the bus back every buffer it still owns before the ring is freed.
//
// Returns only once nothing is outstanding. There is no giving up: freeing a
// buffer libusb still owns invites the device to write into memory something
// else now holds, and no bound on the wait makes that safe. The kernel takes
// the same position -- usb_kill_urb() blocks until the URB has been returned
// and its completion handler has run.
static void
drain_inflight(struct katherine_usb *u)
{
    (void) katherine_mutex_lock(&u->lock);
    u->draining = true;

    cancel_outstanding(u);

    pump_until_quiet(u, DRAIN_POLITE_ATTEMPTS);

    // Nothing came back in the polite window, so say it again and wait it
    // out. Unbounded on purpose: a cancelled transfer completes once somebody
    // hands libusb its events, and that is exactly what this does, so the
    // wait ends on any backend that honours cancellation at all. The kernel
    // makes the same trade -- usb_kill_urb() blocks until the URB has been
    // returned and its completion handler has run -- because freeing a buffer
    // the bus still owns is worse than a close that takes its time.
    if (u->inflight > 0) {
        cancel_outstanding(u);
        pump_until_quiet(u, 0);
    }

    (void) katherine_mutex_unlock(&u->lock);
}

// Every buffer in the ring is the wrong size, so the whole ring is rebuilt.
// The replacements are allocated before any of the originals is given up:
// running out of memory partway then leaves the transport reading at exactly
// the size it already had, which is what the operation promises.
static katherine_error_t
usb_set_buffer_size(void *ctx, size_t size)
{
    katherine_error_t res;
    struct katherine_usb *u = ctx;
    uint8_t **fresh         = NULL;
    uint32_t allocated      = 0;

    if (size == 0) {
        res = KATHERINE_E_INVAL;
        goto err_size;
    }

    // Asked before anything is torn down, because a refusal must leave the
    // ring untouched. Nothing can lend a buffer between here and the swap:
    // lending happens only in acquire_data_buf(), and a caller inside this
    // call is by construction not inside that one.
    if ((res = katherine_mutex_lock(&u->lock)) != KATHERINE_E_OK) goto err_probe_lock;

    bool lent = false;
    for (uint32_t i = 0; i < u->config.queue_len; ++i)
        if (u->slots[i].state == SLOT_LENT) lent = true;

    if ((res = katherine_mutex_unlock(&u->lock)) != KATHERINE_E_OK) goto err_probe_lock;

    if (lent) {
        res = KATHERINE_E_STATE;
        goto err_lent;
    }

    fresh = calloc(u->config.queue_len, sizeof(*fresh));
    if (fresh == NULL) {
        res = KATHERINE_E_NOMEM;
        goto err_table;
    }

    // A whole word past the capacity, for the reason the UDP transport gives
    // at the same allocation: a decoder reads the last six-byte item through
    // an eight-byte load.
    for (; allocated < u->config.queue_len; ++allocated) {
        fresh[allocated] = malloc(size + sizeof(uint64_t));
        if (fresh[allocated] == NULL) {
            res = KATHERINE_E_NOMEM;
            goto err_buffers;
        }
    }

    // libusb is writing into the originals, so none of them can be freed
    // while the bus still holds one.
    if (u->config.stream == KATHERINE_USB_STREAM_ASYNC) drain_inflight(u);

    if ((res = katherine_mutex_lock(&u->lock)) != KATHERINE_E_OK) goto err_buffers;

    for (uint32_t i = 0; i < u->config.queue_len; ++i) {
        free(u->slots[i].buf);
        u->slots[i].buf   = fresh[i];
        u->slots[i].len   = 0;
        u->slots[i].state = SLOT_IDLE;

        if (u->slots[i].xfer != NULL)
            libusb_fill_bulk_transfer(u->slots[i].xfer, u->handle, USB_EP_DATA_IN, u->slots[i].buf, (int) size, on_transfer_done, &u->slots[i], u->config.timeout_ms);
    }

    // Whatever had already come back was in a buffer that is now freed, so
    // the queue of filled slots goes with it. Resizing happens between a link
    // being opened and a measurement being set up on it, where there is
    // nothing yet to discard.
    u->ready_head         = 0;
    u->ready_count        = 0;
    u->draining           = false;
    u->config.buffer_size = (uint32_t) size;

    // Past the swap the fresh buffers are the ring's, so a failure from here
    // on releases the table alone.
    if ((res = katherine_mutex_unlock(&u->lock)) != KATHERINE_E_OK) goto err_swapped;

    // Not resubmitted here. acquire_data_buf() reposts every idle slot before
    // it waits, so the ring refills itself on the next read and this call has
    // no failure of its own to report.
    free(fresh);

    return KATHERINE_E_OK;

err_buffers:
    while (allocated > 0) free(fresh[--allocated]);
err_swapped:
    free(fresh);
err_table:
err_lent:
err_probe_lock:
err_size:
    return res;
}

static void
usb_fini(void *ctx)
{
    struct katherine_usb *u = ctx;
    if (u == NULL) return;

    // Everything below hands memory back to the allocator and tears down the
    // session those transfers point into. If the bus still owns a buffer,
    // doing that invites the device to write into memory something else now
    // holds -- a corruption with no witness, arriving at a time unrelated to
    // the close.
    //
    // So the drain below waits every one of them out, however long that
    // takes. Nothing here can free early and nothing is left behind.
    if (u->config.stream == KATHERINE_USB_STREAM_ASYNC) drain_inflight(u);

    if (u->slots != NULL) {
        for (uint32_t i = 0; i < u->config.queue_len; ++i) {
            if (u->slots[i].xfer != NULL) libusb_free_transfer(u->slots[i].xfer);
            free(u->slots[i].buf);
        }
        free(u->slots);
    }

    free(u->ready);

    if (u->handle != NULL) {
        (void) libusb_release_interface(u->handle, USB_INTERFACE);
        libusb_close(u->handle);
    }

    if (u->ctx != NULL) libusb_exit(u->ctx);

    (void) katherine_mutex_fini(&u->lock);
    (void) katherine_mutex_fini(&u->api_lock);
    free(u);
}

static const katherine_transport_ops_t usb_ops_sync = {
    .send_cmd         = usb_send_cmd,
    .recv_crd         = usb_recv_crd,
    .flush_incoming   = usb_flush_incoming,
    .acquire_data_buf = usb_acquire_sync,
    .release_data_buf = usb_release_sync,
    .set_timeout      = usb_set_timeout,
    .set_buffer_size  = usb_set_buffer_size,
    .mutex_lock       = usb_mutex_lock,
    .mutex_unlock     = usb_mutex_unlock,
    .last_os_error    = usb_last_os_error,
    .fini             = usb_fini,
};

static const katherine_transport_ops_t usb_ops_async = {
    .send_cmd         = usb_send_cmd,
    .recv_crd         = usb_recv_crd,
    .flush_incoming   = usb_flush_incoming,
    .acquire_data_buf = usb_acquire_async,
    .release_data_buf = usb_release_async,
    .set_timeout      = usb_set_timeout,
    .set_buffer_size  = usb_set_buffer_size,
    .mutex_lock       = usb_mutex_lock,
    .mutex_unlock     = usb_mutex_unlock,
    .last_os_error    = usb_last_os_error,
    .fini             = usb_fini,
};

// A readout is named by where it sits: the bus, then the chain of port
// numbers from the root hub down to it. Spelled the way Linux names the same
// device in /sys/bus/usb/devices, so "2-3.7.1.1" is what a reader sees in
// lsusb -t and can paste where it is wanted.
//
// It is a socket address rather than an identity. It survives a replug into
// the same socket and a device reset, which the address the host assigns at
// enumeration does not; it changes when the cable moves or a hub is inserted
// above it.
// One decimal field of an address, which is a port number or a bus and so
// never wider than a byte. Returns where it stopped, or NULL for anything
// that is not such a field.
static const char *
usb_read_u8(const char *text, uint32_t *out)
{
    uint32_t value = 0;
    const char *p  = text;

    for (; *p >= '0' && *p <= '9'; ++p) {
        value = value * 10 + (uint32_t) (*p - '0');
        if (value > UINT8_MAX) return NULL;
    }

    if (p == text) return NULL;

    *out = value;

    return p;
}

static katherine_error_t
usb_location_parse(const char *text, uint8_t *bus, uint8_t *ports, uint8_t *port_count)
{
    katherine_error_t res;
    uint32_t value;
    const char *end;

    // The one readout attached, for a caller that would rather not pin a
    // socket. usb_open_matching() is what refuses it where a second one is
    // there, since the address then names no particular readout.
    if (strcmp(text, USB_LOCATION_AUTO) == 0) {
        *bus        = 0;
        *port_count = 0;

        return KATHERINE_E_OK;
    }

    end = usb_read_u8(text, &value);
    if (end == NULL || *end != '-') {
        res = KATHERINE_E_ADDRESS;
        goto err_bus;
    }

    *bus        = (uint8_t) value;
    *port_count = 0;

    for (const char *p = end + 1;;) {
        end = usb_read_u8(p, &value);
        if (end == NULL) {
            res = KATHERINE_E_ADDRESS;
            goto err_port;
        }
        // A port number counts from one, so a zero is a misread rather than
        // the root: libusb never reports one.
        if (value == 0) {
            res = KATHERINE_E_ADDRESS;
            goto err_port;
        }
        if (*port_count >= KATHERINE_USB_PORT_DEPTH) {
            res = KATHERINE_E_ADDRESS;
            goto err_depth;
        }

        ports[(*port_count)++] = (uint8_t) value;

        if (*end == '\0') break;
        if (*end != '.') {
            res = KATHERINE_E_ADDRESS;
            goto err_separator;
        }
        p = end + 1;
    }

    return KATHERINE_E_OK;

err_separator:
err_depth:
err_port:
err_bus:
    return res;
}

static int
usb_location_format(char *buf, size_t cap, uint8_t bus, const uint8_t *ports, uint8_t port_count)
{
    int off = snprintf(buf, cap, "%u", (unsigned) bus);

    for (uint8_t i = 0; i < port_count && off >= 0 && (size_t) off < cap; ++i)
        off += snprintf(buf + off, cap - (size_t) off, "%c%u", i == 0 ? '-' : '.', (unsigned) ports[i]);

    return off;
}

// True where this device is the one the configuration names. The identifiers
// are checked always; the location only when one was given, so a
// configuration that names none still takes whichever device carries them.
//
// libusb_open_device_with_vid_pid(), which this replaces, takes the first
// match and offers no way to say which -- so two readouts on one host were
// indistinguishable, and which one a program got depended on enumeration
// order.
static bool
usb_device_matches(libusb_device *device, const katherine_usb_config_t *config)
{
    struct libusb_device_descriptor descriptor;

    if (libusb_get_device_descriptor(device, &descriptor) != LIBUSB_SUCCESS) return false;
    if (descriptor.idVendor != USB_VENDOR_ID || descriptor.idProduct != USB_PRODUCT_ID) return false;

    // No chain asked for: any readout answers, and usb_open_matching() is
    // what refuses the case where more than one does.
    if (config->port_count == 0) return true;

    if (libusb_get_bus_number(device) != config->bus) return false;

    uint8_t ports[KATHERINE_USB_PORT_DEPTH];
    const int depth = libusb_get_port_numbers(device, ports, (int) sizeof(ports));

    // The whole chain, not its last element: two readouts in port 1 of two
    // different hubs share that and nothing else.
    if (depth <= 0 || (uint8_t) depth != config->port_count) return false;

    return memcmp(ports, config->ports, config->port_count) == 0;
}

// Caller owns closing the handle. The list is released before returning, as
// libusb_get_device_list() requires, which is safe because opening the device
// takes a reference of its own.
static katherine_error_t
usb_open_matching(struct katherine_usb *u, const katherine_usb_config_t *config)
{
    katherine_error_t res;
    libusb_device **list = NULL;

    const ssize_t count = libusb_get_device_list(u->ctx, &list);
    if (count < 0) {
        res = map_usb_error(u, (int) count);
        goto err_list;
    }

    // Counted before anything is opened: an address naming no socket means
    // "the one readout on this host", and two of them make that a question
    // with no answer rather than a choice to make arbitrarily.
    libusb_device *chosen = NULL;
    unsigned matched      = 0;

    for (ssize_t i = 0; i < count; ++i) {
        if (!usb_device_matches(list[i], config)) continue;

        matched += 1;
        if (chosen == NULL) chosen = list[i];
    }

    if (chosen == NULL || (config->port_count == 0 && matched > 1)) {
        res = KATHERINE_E_ADDRESS;
        goto err_open;
    }

    const int code = libusb_open(chosen, &u->handle);
    if (code != LIBUSB_SUCCESS) {
        res = map_usb_error(u, code);
        goto err_open;
    }

    libusb_free_device_list(list, 1);
    return KATHERINE_E_OK;

err_open:
    libusb_free_device_list(list, 1);
err_list:
    return res;
}

/**
 * Point a configuration at one readout by where it sits on the bus.
 * \param config Configuration to set the location on.
 * \param address Address naming it, with or without the scheme:
 *   "usb://2-3.7.1.1" or "2-3.7.1.1". The location "auto", as in
 *   "usb://auto", names the one readout attached instead of a socket, for a
 *   caller with a single device that would rather not pin its port chain;
 *   katherine_transport_init_usb() refuses it where a second one is there.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_ADDRESS if config or address is NULL, the address names
 *   a link that is not USB, or its location is neither "auto" nor a bus
 *   number followed by between one and KATHERINE_USB_PORT_DEPTH port numbers.
 */
katherine_error_t
katherine_usb_config_set_address(katherine_usb_config_t *config, const char *address)
{
    katherine_error_t res;
    katherine_transport_kind_t kind;
    const char *rest;

    if (config == NULL) {
        res = KATHERINE_E_ADDRESS;
        goto err_config;
    }

    res = katherine_address_split(address, &kind, &rest);
    if (res) goto err_split;

    if (kind != KATHERINE_TRANSPORT_USB) {
        res = KATHERINE_E_ADDRESS;
        goto err_kind;
    }

    res = usb_location_parse(rest, &config->bus, config->ports, &config->port_count);
    if (res) goto err_location;

    return KATHERINE_E_OK;

err_location:
err_kind:
err_split:
err_config:
    return res;
}

/**
 * Write the address naming the readout a configuration points at.
 * \param buf Destination.
 * \param cap Bytes available at \p buf.
 * \param config Configuration to render.
 *
 * \return What snprintf(3) returns, so a value of \p cap or more says the
 *   address was truncated. Negative where the configuration names no
 *   location at all.
 */
int
katherine_usb_config_snprint_address(char *buf, size_t cap, const katherine_usb_config_t *config)
{
    if (config == NULL || config->port_count == 0) return -1;

    char where[KATHERINE_DEVICE_ADDRESS_STR_SIZE];
    if (usb_location_format(where, sizeof(where), config->bus, config->ports, config->port_count) < 0) return -1;

    return katherine_address_format(buf, cap, KATHERINE_TRANSPORT_USB, where);
}

/**
 * Render a read strategy as the token naming it.
 * \param stream Strategy to render.
 *
 * \return The token, or "?" for a value this version does not name.
 */
const char *
katherine_str_usb_stream(katherine_usb_stream_t stream)
{
    switch (stream) {
    case KATHERINE_USB_STREAM_ASYNC: return "async";
    case KATHERINE_USB_STREAM_SYNC:  return "sync";
    default:                         return "?";
    }
}

/**
 * The geometry the reference implementation uses for this readout: 64 reads
 * of 48 KiB, timing out after 100 ms. The identifiers are left at zero, there
 * being no readout this library knows of by default.
 *
 * \return The configuration.
 */
katherine_usb_config_t
katherine_usb_config_default(void)
{
    // The geometry the reference implementation runs this readout at.
    katherine_usb_config_t config = {
        .stream      = KATHERINE_USB_STREAM_ASYNC,
        .queue_len   = 64,
        .buffer_size = 48 * 1024,
        .timeout_ms  = 100,
    };

    return config;
}

/**
 * Open a readout over USB, filling in a transport that drives it.
 * \param transport Transport to fill in.
 * \param config Readout to open, and how to read it.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if the configuration asks for no buffers, for
 *   empty ones, or names no measurement-data endpoint.
 * \retval KATHERINE_E_NOMEM if the buffers or the libusb session could not be
 *   allocated.
 * \retval KATHERINE_E_ADDRESS if no device on the bus carries the requested
 *   vendor and product identifiers.
 * \retval KATHERINE_E_CLOSED if the interface the configuration names does
 *   not exist on the device, or the device went away between being opened
 *   and its interface being claimed; see katherine_transport_last_os_error()
 *   for the libusb code behind it.
 * \retval KATHERINE_E_IO if the device could not be opened or its interface
 *   claimed; see katherine_transport_last_os_error().
  * \note Unreachable here: KATHERINE_E_TIMEOUT. The three calls in this
 *   function that reach the error mapper are libusb_init_context(),
 *   libusb_claim_interface() and libusb_submit_transfer(), and none of
 *   them returns LIBUSB_ERROR_TIMEOUT. The code is reachable through the
 *   mapper, not through this caller.
 * \retval KATHERINE_E_SYSTEM if one of the ring's locks could not be created,
 *   taken or released; see pthread_mutex_init(3) and
 *   katherine_transport_last_os_error().
 */
katherine_error_t
katherine_transport_init_usb(katherine_transport_t *transport, const katherine_usb_config_t *config)
{
    katherine_error_t res;

    if (transport == NULL || config == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_args;
    }

    if (config->buffer_size == 0) {
        res = KATHERINE_E_INVAL;
        goto err_config;
    }

    // The synchronous shape reads into one buffer, whatever depth was asked
    // for: there is never more than one read outstanding to fill a second.
    const uint32_t slots = config->stream == KATHERINE_USB_STREAM_SYNC ? 1 : config->queue_len;
    if (slots == 0) {
        res = KATHERINE_E_INVAL;
        goto err_config;
    }

    struct katherine_usb *u = calloc(1, sizeof(*u));
    if (u == NULL) {
        res = KATHERINE_E_NOMEM;
        goto err_alloc;
    }

    u->config           = *config;
    u->config.queue_len = slots;

    if ((res = katherine_mutex_init(&u->lock)) != KATHERINE_E_OK) goto err_lock_init;
    if ((res = katherine_mutex_init(&u->api_lock)) != KATHERINE_E_OK) goto err_api_lock_init;

    u->slots = calloc(slots, sizeof(*u->slots));
    u->ready = calloc(slots, sizeof(*u->ready));
    if (u->slots == NULL || u->ready == NULL) {
        res = KATHERINE_E_NOMEM;
        goto err_slots;
    }

    // A private session, so that code elsewhere in the process handling events
    // on the default context cannot reach these transfers.
    int code = libusb_init_context(&u->ctx, NULL, 0);
    if (code != LIBUSB_SUCCESS) {
        res = map_usb_error(u, code);
        goto err_init;
    }

    res = usb_open_matching(u, config);
    if (res) goto err_open;

    (void) libusb_set_auto_detach_kernel_driver(u->handle, 1);

    code = libusb_claim_interface(u->handle, USB_INTERFACE);
    if (code != LIBUSB_SUCCESS) {
        res = map_usb_error(u, code);
        goto err_claim;
    }

    for (uint32_t i = 0; i < slots; ++i) {
        u->slots[i].index = i;
        u->slots[i].usb   = u;
        u->slots[i].buf   = malloc(config->buffer_size + sizeof(uint64_t));
        if (u->slots[i].buf == NULL) {
            res = KATHERINE_E_NOMEM;
            goto err_buffers;
        }

        if (config->stream == KATHERINE_USB_STREAM_SYNC) continue;

        u->slots[i].xfer = libusb_alloc_transfer(0);
        if (u->slots[i].xfer == NULL) {
            res = KATHERINE_E_NOMEM;
            goto err_buffers;
        }

        libusb_fill_bulk_transfer(u->slots[i].xfer, u->handle, USB_EP_DATA_IN, u->slots[i].buf, (int) config->buffer_size, on_transfer_done, &u->slots[i], config->timeout_ms);
    }

    if (config->stream == KATHERINE_USB_STREAM_ASYNC) {
        // Released before err_submit below, which drains the ring and so
        // takes this very lock.
        bool held = false;

        if ((res = katherine_mutex_lock(&u->lock)) != KATHERINE_E_OK) goto err_submit;
        held = true;

        for (uint32_t i = 0; i < slots; ++i) {
            res = submit_slot(u, &u->slots[i], &held);
            if (res) {
                if (held) (void) katherine_mutex_unlock(&u->lock);
                goto err_submit;
            }
        }

        if ((res = katherine_mutex_unlock(&u->lock)) != KATHERINE_E_OK) goto err_submit;
    }

    transport->ops  = config->stream == KATHERINE_USB_STREAM_SYNC ? &usb_ops_sync : &usb_ops_async;
    transport->kind = KATHERINE_TRANSPORT_USB;
    transport->ctx  = u;

    return KATHERINE_E_OK;

err_submit:
    drain_inflight(u);
err_buffers:
    // Before the handle and the context go: libusb_free_transfer() reads the
    // transfer's device handle and takes the context's own lock, so freeing
    // here is what reverse order of acquisition means. usb_fini() already
    // does it this way round.
    for (uint32_t i = 0; i < slots; ++i) {
        if (u->slots[i].xfer != NULL) libusb_free_transfer(u->slots[i].xfer);
        free(u->slots[i].buf);
    }
    (void) libusb_release_interface(u->handle, USB_INTERFACE);
err_claim:
    libusb_close(u->handle);
err_open:
    libusb_exit(u->ctx);
err_init:
err_slots:
    free(u->slots);
    free(u->ready);
    (void) katherine_mutex_fini(&u->api_lock);
err_api_lock_init:
    (void) katherine_mutex_fini(&u->lock);
err_lock_init:
    free(u);
err_alloc:
err_config:
err_args:
    return res;
}

/**
 * Ask the bus which readouts carrying these identifiers are attached, and
 * under what address each can be opened.
 *
 * Nothing is opened: the walk reads descriptors and topology only, so it
 * works before a udev rule grants access and never touches a kernel driver.
 * That is the difference from katherine_device_discover_udp(), which has to
 * put a datagram on the wire to find anything.
 *
 * \param vendor_id USB vendor identifier to look for.
 * \param product_id USB product identifier to look for.
 * \param out Storage for the answers, of at least the entries \p count gives.
 * \param count On entry the capacity of \p out, on return the number of
 *   entries written, which is zero on every error.
 *
 * \retval KATHERINE_E_OK on success, including when nothing is attached.
 * \retval KATHERINE_E_INVAL if out or count is NULL, or the capacity count
 *   gives is zero.
 * \retval KATHERINE_E_NOMEM if the libusb session could not be allocated.
 * \retval KATHERINE_E_IO if the session or the device list could not be had;
 *   see libusb_init_context() and libusb_get_device_list().
 */
katherine_error_t
katherine_device_discover_usb(katherine_discovered_t *out, size_t *count)
{
    katherine_error_t res;
    libusb_context *ctx  = NULL;
    libusb_device **list = NULL;

    if (out == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_out;
    }
    if (count == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_count;
    }

    const size_t capacity = *count;
    *count                = 0;

    if (capacity == 0) {
        res = KATHERINE_E_INVAL;
        goto err_capacity;
    }

    int code = libusb_init_context(&ctx, NULL, 0);
    if (code != LIBUSB_SUCCESS) {
        res = code == LIBUSB_ERROR_NO_MEM ? KATHERINE_E_NOMEM : KATHERINE_E_IO;
        goto err_init;
    }

    const ssize_t found = libusb_get_device_list(ctx, &list);
    if (found < 0) {
        res = found == LIBUSB_ERROR_NO_MEM ? KATHERINE_E_NOMEM : KATHERINE_E_IO;
        goto err_list;
    }

    // Any port chain: every readout carrying the identifiers is reported,
    // and the address handed back is what tells them apart.
    katherine_usb_config_t probe = {.port_count = 0};

    for (ssize_t i = 0; i < found && *count < capacity; ++i) {
        if (!usb_device_matches(list[i], &probe)) continue;

        katherine_discovered_t *entry = &out[*count];
        memset(entry, 0, sizeof(*entry));

        uint8_t ports[KATHERINE_USB_PORT_DEPTH];
        const int depth = libusb_get_port_numbers(list[i], ports, (int) sizeof(ports));

        // A device deeper than the specification allows, or one whose
        // topology the host will not report, has no address this library can
        // hand back -- so it is left out rather than reported under a name
        // that would not open it.
        if (depth <= 0) continue;

        char where[KATHERINE_DEVICE_ADDRESS_STR_SIZE];
        if (usb_location_format(where, sizeof(where), libusb_get_bus_number(list[i]), ports, (uint8_t) depth) < 0)
            continue;

        const int written = katherine_address_format(entry->address, sizeof(entry->address), KATHERINE_TRANSPORT_USB, where);
        if (written < 0 || (size_t) written >= sizeof(entry->address)) continue;

        // A bus has no reply-port behaviour to have an opinion about.
        entry->legacy = false;
        *count += 1;
    }

    res = KATHERINE_E_OK;

err_list:
    if (list != NULL) libusb_free_device_list(list, 1);
    libusb_exit(ctx);
err_init:
err_capacity:
err_count:
err_out:
    return res;
}

/**
 * Reads outstanding on the measurement-data endpoint.
 * \param transport Transport to ask.
 * \param count Reads outstanding, zero for a synchronous transport.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_INVAL if \a transport is not an open transport, or
 *   \a count is NULL.
 * \retval KATHERINE_E_SYSTEM if the ring's lock could not be taken or
 *   released; see pthread_mutex_lock(3) and
 *   katherine_transport_last_os_error().
 */
katherine_error_t
katherine_usb_inflight(const katherine_transport_t *transport, uint32_t *count)
{
    katherine_error_t res;

    if (transport == NULL || transport->ctx == NULL || count == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_args;
    }

    struct katherine_usb *u = transport->ctx;

    if ((res = katherine_mutex_lock(&u->lock)) != KATHERINE_E_OK) goto err_lock;

    *count = u->inflight;

    if ((res = katherine_mutex_unlock(&u->lock)) != KATHERINE_E_OK) goto err_unlock;

    return KATHERINE_E_OK;

err_unlock:
err_lock:
err_args:
    return res;
}

#endif /* KATHERINE_FEATURE_USB */
