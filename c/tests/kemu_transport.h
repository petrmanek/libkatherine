/**
 * \file
 * \brief The protocol emulator behind a transport, in this process.
 *
 * ksim puts the emulator behind a socket and a second process, which is
 * what an end-to-end test wants. This puts it behind a
 * katherine_transport_ops_t instead: no sockets, no ports, no threads, no
 * RUN_SERIAL, and the virtual clock advances only when a call asks it to.
 *
 * What it buys that ksim cannot is the failures a *link* has rather than
 * the ones a readout has. A readout that goes silent is ksim's
 * --mute-opcode; a send that fails at the OS level is not something a peer
 * can arrange, and it is the only way into several of this library's error
 * paths -- katherine_acquisition_begin()'s window between the control lock
 * and the start command, for one, which nothing could reach before this.
 *
 * Not a general fixture yet: it serves the control plane. The data plane
 * still comes from a real UDP session, which the data factory opens from
 * the device's address without reading the control transport's context.
 *
 * \author Petr Mánek
 * \date 25.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <string.h>

#include <katherine/emulator.h>
#include <katherine/katherine.h>

#include "transport/ops.h"

/** How far the virtual clock moves per exchange. Past any latency a profile sets. */
#define KEMU_TICK_NS    1000000u

/** Attempts a receive makes before reporting a timeout. */
#define KEMU_RECV_TRIES 64

/** The emulator, the faults to inject into it, and what it has seen. */
typedef struct kemu_transport {
    katherine_emu_t emu;

    /**
     * Operation code whose *send* fails, zero for none. Distinct from the
     * profile's mute_opcode, which drops the command after a successful
     * send: this one is the link refusing to carry it.
     */
    uint16_t fail_send_opcode;

    /** What that send answers. */
    katherine_error_t fail_send_code;

    uint32_t sends;    ///< Commands handed to the link, failed ones included.
    uint32_t failures; ///< Of those, the ones the injection refused.
    uint8_t last[8];   ///< The last command that reached the emulator.
} kemu_transport_t;

static inline katherine_error_t
kemu_lock(void *ctx)
{
    (void) ctx;
    return KATHERINE_E_OK;
}

static inline katherine_error_t
kemu_send_cmd(void *ctx, const void *data, size_t count)
{
    kemu_transport_t *t  = (kemu_transport_t *) ctx;
    const uint8_t *bytes = (const uint8_t *) data;
    ++t->sends;

    if (t->fail_send_opcode != 0 && count >= 8) {
        const uint16_t opcode = (uint16_t) (bytes[6] | (bytes[7] << 8));
        if (opcode == t->fail_send_opcode) {
            ++t->failures;
            return t->fail_send_code;
        }
    }

    if (count >= 8) memcpy(t->last, bytes, 8);

    const katherine_error_t res = katherine_emu_cmd_in(&t->emu, data, count);
    katherine_emu_advance(&t->emu, KEMU_TICK_NS);
    return res;
}

static inline katherine_error_t
kemu_recv_crd(void *ctx, void *crd, size_t *len)
{
    kemu_transport_t *t = (kemu_transport_t *) ctx;

    for (int i = 0; i < KEMU_RECV_TRIES; ++i) {
        if (katherine_emu_crd_out(&t->emu, crd, len) == KATHERINE_E_OK) return KATHERINE_E_OK;
        katherine_emu_advance(&t->emu, KEMU_TICK_NS);
    }

    return KATHERINE_E_TIMEOUT;
}

static inline katherine_error_t
kemu_flush_incoming(void *ctx, uint32_t *discarded)
{
    kemu_transport_t *t = (kemu_transport_t *) ctx;
    uint32_t n          = 0;
    char crd[KATHERINE_EMU_CRD_SIZE];

    while (katherine_emu_crd_out(&t->emu, crd, NULL) == KATHERINE_E_OK) ++n;

    if (discarded != NULL) *discarded = n;
    return KATHERINE_E_OK;
}

static inline katherine_error_t
kemu_set_timeout(void *ctx, uint32_t timeout_ms)
{
    (void) ctx;
    (void) timeout_ms;
    return KATHERINE_E_OK;
}

static inline katherine_error_t
kemu_get_dropped_input(void *ctx, uint64_t *count)
{
    kemu_transport_t *t = (kemu_transport_t *) ctx;
    if (count != NULL) *count = katherine_emu_dropped_crd_count(&t->emu);
    return KATHERINE_E_OK;
}

static inline int
kemu_last_os_error(void *ctx)
{
    (void) ctx;
    return 0;
}

static const katherine_transport_ops_t KEMU_OPS = {
    .send_cmd          = kemu_send_cmd,
    .recv_crd          = kemu_recv_crd,
    .flush_incoming    = kemu_flush_incoming,
    .set_timeout       = kemu_set_timeout,
    .get_dropped_input = kemu_get_dropped_input,
    .mutex_lock        = kemu_lock,
    .mutex_unlock      = kemu_lock,
    .get_last_os_error = kemu_last_os_error,
};

/**
 * A device driven by the emulator over the control plane.
 *
 * Assembled rather than opened: katherine_device_declare() gives it the
 * generation the profile claims, and the transport is installed by hand.
 * The address is a real one because katherine_acquisition_init() opens the
 * data plane from it -- that one is an ordinary UDP session, bound and
 * never spoken to.
 */
static inline katherine_error_t
kemu_device(katherine_device_t *device, kemu_transport_t *t, const katherine_emu_profile_t *profile)
{
    memset(device, 0, sizeof(*device));
    memset(t, 0, sizeof(*t));

    const katherine_error_t res = katherine_emu_init(&t->emu, profile);
    if (res != KATHERINE_E_OK) return res;

    strcpy(device->address, "udp://127.0.0.1");
    device->control_transport.ops  = &KEMU_OPS;
    device->control_transport.ctx  = t;
    device->control_transport.kind = KATHERINE_TRANSPORT_UDP;

    const katherine_device_info_t info = {
        .hw_type       = profile->hw_type,
        .hw_revision   = profile->hw_revision,
        .serial_number = profile->serial,
        .fw_version    = profile->fw_version,
        .chip_count    = profile->chip_count == 0 ? 1 : profile->chip_count,
    };

    return katherine_device_declare(device, &info);
}

/** Releases what kemu_device() set up. The device itself owns no link. */
static inline void
kemu_device_fini(katherine_device_t *device, kemu_transport_t *t)
{
    device->control_transport.ops = NULL;
    device->control_transport.ctx = NULL;
    katherine_emu_fini(&t->emu);
}

// ------------------------------------------------------------------
// The data plane.
//
// katherine_acquisition_init() opens a real UDP session from the device's
// address, which binds a port and is never spoken to. A test that wants
// hits replaces it with this after init: one buffer of measurement data,
// served once, and a timeout from then on, so the read loop ends on its
// own fail_timeout rather than running forever. The acquire and release
// counts are here because every acquired buffer has to come back.

/** One stream of measurement data, and what the loop did with it. */
typedef struct kemu_data {
    const void *md; ///< Measurement data to serve, whole, once.
    size_t len;     ///< Its length in bytes, a multiple of KATHERINE_MD_SIZE.
    int served;     ///< Times the buffer has been handed over.
    int acquires;   ///< Buffers lent to the read loop.
    int releases;   ///< Buffers it gave back; equal to acquires on every path.
} kemu_data_t;

static inline katherine_error_t
kemu_data_acquire(void *ctx, void **buf, size_t *len)
{
    kemu_data_t *d = (kemu_data_t *) ctx;

    if (d->served++ > 0) return KATHERINE_E_TIMEOUT;

    ++d->acquires;
    *buf = (void *) (uintptr_t) d->md;
    *len = d->len;
    return KATHERINE_E_OK;
}

static inline katherine_error_t
kemu_data_release(void *ctx, void *buf)
{
    kemu_data_t *d = (kemu_data_t *) ctx;
    (void) buf;
    ++d->releases;
    return KATHERINE_E_OK;
}

static inline katherine_error_t
kemu_data_dropped(void *ctx, uint64_t *count)
{
    (void) ctx;
    if (count != NULL) *count = 0;
    return KATHERINE_E_OK;
}

static const katherine_transport_ops_t KEMU_DATA_OPS = {
    .acquire_data_buf  = kemu_data_acquire,
    .release_data_buf  = kemu_data_release,
    .set_timeout       = kemu_set_timeout,
    .get_dropped_input = kemu_data_dropped,
    .mutex_lock        = kemu_lock,
    .mutex_unlock      = kemu_lock,
    .get_last_os_error = kemu_last_os_error,
};

/**
 * Puts the stream behind the acquisition's data plane, in place of the UDP
 * session katherine_acquisition_init() opened. Call after init and before
 * katherine_acquisition_begin(); kemu_data_detach() before finalizing.
 */
static inline void
kemu_data_attach(katherine_acquisition_t *acq, kemu_data_t *data)
{
    katherine_transport_fini(&acq->data_transport);
    acq->data_transport.ops = &KEMU_DATA_OPS;
    acq->data_transport.ctx = data;
}

/** Takes it away again, so katherine_acquisition_fini() finalizes nothing. */
static inline void
kemu_data_detach(katherine_acquisition_t *acq)
{
    acq->data_transport.ops = NULL;
    acq->data_transport.ctx = NULL;
}
