/**
 * \file
 * \brief The link a readout is driven over, behind one set of operations.
 *
 * \author Petr Mánek
 * \date 14.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <katherine/global.h>
#include <katherine/error.h>

/**
 * \defgroup katherine_transport Transport
 * \ingroup katherine_c_api
 * \brief Driving a readout over a variety of physical links, which carry
 *   the same communication protocol.
 */

/**
 * \addtogroup katherine_transport
 * \{
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Kind of link carrying a readout's traffic. */
typedef enum katherine_transport_kind {
    KATHERINE_TRANSPORT_UDP = 0, ///< UDP datagrams over an IP network, typically 1 Gbps Ethernet. May be lossy in high-flux conditions.
    KATHERINE_TRANSPORT_USB,     ///< Bulk endpoints on a USB, typically 5 Gbps SuperSpeed (or SuperSpeed+) link.
} katherine_transport_kind_t;

/**
 * Operation set common to every transport.
 *
 * The control plane follows a synchronous scheme: a request followed by its
 * acknowledgement. The data plane is optimized for throughput and so is
 * asynchronous, lending buffers rather than filling the caller's. The
 * transport owns them, hands over a filled one and takes it back, which is
 * what lets an implementation keep several reads outstanding while the
 * caller decodes.
 *
 * Each member is documented on the katherine_transport_* function that
 * calls it, which is also how the rest of the library reaches it.
 */
typedef struct katherine_transport_ops {
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    katherine_error_t (*send_cmd)(void *ctx, const void *data, size_t count);
    katherine_error_t (*recv_crd)(void *ctx, void *crd, size_t count);
    katherine_error_t (*flush_incoming)(void *ctx);
    katherine_error_t (*acquire_data_buf)(void *ctx, void **buf, size_t *len);
    katherine_error_t (*release_data_buf)(void *ctx, void *buf);
    katherine_error_t (*set_timeout)(void *ctx, uint32_t timeout_ms);
    katherine_error_t (*set_buffer_size)(void *ctx, size_t size);
    void (*fini)(void *ctx);
#endif /* DOXYGEN_SHOULD_SKIP_THIS */
} katherine_transport_ops_t;

/** A single communications link, and the operations driving it. */
typedef struct katherine_transport {
    const katherine_transport_ops_t *ops; ///< Low-level operations implementing this transport, essentially a vtable of functions.
    katherine_transport_kind_t kind;      ///< Kind of link behind this transport.
    void *ctx;                            ///< The implementation's own state, passed to every operation.
} katherine_transport_t;

#ifndef DOXYGEN_SHOULD_SKIP_THIS
typedef struct katherine_udp katherine_udp_t;
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

KATHERINE_EXPORTED katherine_error_t
katherine_transport_init_udp(katherine_transport_t *transport, katherine_udp_t *udp, size_t buffer_size);

KATHERINE_EXPORTED katherine_error_t
katherine_transport_send_cmd(katherine_transport_t *transport, const void *data, size_t count);

KATHERINE_EXPORTED katherine_error_t
katherine_transport_recv_crd(katherine_transport_t *transport, void *crd, size_t count);

KATHERINE_EXPORTED katherine_error_t
katherine_transport_flush_incoming(katherine_transport_t *transport);

KATHERINE_EXPORTED katherine_error_t
katherine_transport_acquire_data_buf(katherine_transport_t *transport, void **buf, size_t *len);

KATHERINE_EXPORTED katherine_error_t
katherine_transport_release_data_buf(katherine_transport_t *transport, void *buf);

KATHERINE_EXPORTED katherine_error_t
katherine_transport_set_timeout(katherine_transport_t *transport, uint32_t timeout_ms);

KATHERINE_EXPORTED katherine_error_t
katherine_transport_set_buffer_size(katherine_transport_t *transport, size_t size);

KATHERINE_EXPORTED void
katherine_transport_fini(katherine_transport_t *transport);

#ifdef __cplusplus
}
#endif

/** \} */
