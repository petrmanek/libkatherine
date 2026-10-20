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

#include <stdbool.h>
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

/**
 * Buffer an address needs, counting the terminator. Long enough for the
 * longest form a transport writes: a scheme, "://", a bus number and a chain
 * of up to seven USB port numbers.
 */
#define KATHERINE_DEVICE_ADDRESS_STR_SIZE 64

/**
 * One readout that answered a discovery.
 *
 * Common to every link: what differs is how a transport goes looking, not
 * what it finds. See katherine_device_discover_udp() and
 * katherine_device_discover_usb().
 */
typedef struct katherine_discovered {
    char address[KATHERINE_DEVICE_ADDRESS_STR_SIZE]; ///< Address it answered from, naming the link too: "udp://192.168.1.42" or "usb://4-1". Ready to hand to katherine_device_init().
    bool legacy;                                     ///< True if the firmware ignored the reply port and answered on the standard one. UDP's; false on a link with no such notion.
} katherine_discovered_t;

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
    katherine_error_t (*recv_crd)(void *ctx, void *crd, size_t *len);
    katherine_error_t (*flush_incoming)(void *ctx, uint32_t *discarded);
    katherine_error_t (*acquire_data_buf)(void *ctx, void **buf, size_t *len);
    katherine_error_t (*release_data_buf)(void *ctx, void *buf);
    katherine_error_t (*set_timeout)(void *ctx, uint32_t timeout_ms);
    katherine_error_t (*set_buffer_size)(void *ctx, size_t size);
    katherine_error_t (*get_bound_udp_port)(void *ctx, uint16_t *port);
    katherine_error_t (*mutex_lock)(void *ctx);
    katherine_error_t (*mutex_unlock)(void *ctx);
    int (*last_os_error)(void *ctx);
    void (*fini)(void *ctx);
#endif /* DOXYGEN_SHOULD_SKIP_THIS */
} katherine_transport_ops_t;

/** A single communications link, and the operations driving it. */
typedef struct katherine_transport {
    const katherine_transport_ops_t *ops; ///< Low-level operations implementing this transport, essentially a vtable of functions.
    katherine_transport_kind_t kind;      ///< Kind of link behind this transport.
    void *ctx;                            ///< The implementation's own state, passed to every operation.

    /**
     * True to require a command response to repeat the operation code of its
     * request exactly, rather than accepting the reply identifier derived
     * from it. Set through katherine_transport_set_strict_ack().
     */
    bool strict_ack;

    /**
     * Command responses discarded because they belonged to no request in
     * flight. A shared network delivers these for reasons that are nobody's
     * fault; a link that cannot deliver them leaves this at zero.
     */
    uint64_t stray_command_responses;
} katherine_transport_t;

#ifndef DOXYGEN_SHOULD_SKIP_THIS
typedef struct katherine_udp katherine_udp_t;
typedef struct katherine_udp_config katherine_udp_config_t;
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

KATHERINE_EXPORTED katherine_error_t
katherine_transport_init_udp(katherine_transport_t *transport, const katherine_udp_config_t *config);

KATHERINE_EXPORTED katherine_error_t
katherine_transport_send_cmd(katherine_transport_t *transport, const void *data, size_t count);

KATHERINE_EXPORTED katherine_error_t
katherine_transport_recv_crd(katherine_transport_t *transport, void *crd, size_t *len);

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

KATHERINE_EXPORTED katherine_error_t
katherine_transport_get_bound_udp_port(const katherine_transport_t *transport, uint16_t *port);

KATHERINE_EXPORTED katherine_error_t
katherine_transport_mutex_lock(katherine_transport_t *transport);

KATHERINE_EXPORTED katherine_error_t
katherine_transport_mutex_unlock(katherine_transport_t *transport);

KATHERINE_EXPORTED int
katherine_transport_last_os_error(const katherine_transport_t *transport);

KATHERINE_EXPORTED void
katherine_transport_set_strict_ack(katherine_transport_t *transport, bool strict);

KATHERINE_EXPORTED const char *
katherine_str_transport_kind(katherine_transport_kind_t kind);

KATHERINE_EXPORTED int
katherine_transport_snprint(char *buf, size_t cap, const katherine_transport_t *v);

KATHERINE_EXPORTED void
katherine_transport_fini(katherine_transport_t *transport);

#ifdef __cplusplus
}
#endif

/** \} */
