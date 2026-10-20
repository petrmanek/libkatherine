/**
 * \file
 * \brief Internal construction of the two transports a device drives.
 * \author Petr Mánek
 * \date 20.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <katherine/error.h>
#include <katherine/global.h>
#include <katherine/transport.h>
#include "udp.h"

#include "../address.h"

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

// The standard pair a readout serves, and the port it listens on.
#define KATHERINE_UDP_CONTROL_PORT    ((uint16_t) 1555)
#define KATHERINE_UDP_DATA_PORT       ((uint16_t) 1556)
#define KATHERINE_UDP_REMOTE_PORT     ((uint16_t) 1555)

// What a session waits for an answer that may never come.
#define KATHERINE_UDP_CONTROL_TIMEOUT ((uint32_t) 100)
#define KATHERINE_UDP_DATA_TIMEOUT    ((uint32_t) 100)

/**
 * Open the control and measurement-data sessions of one readout.
 * \param host Readout both sessions address, without the scheme.
 * \param should_change_udp_ports True to bind local ports of the operating
 *   system's choosing instead of KATHERINE_UDP_CONTROL_PORT and
 *   KATHERINE_UDP_DATA_PORT.
 * \param control Transport filled in with the command role.
 * \param data Transport filled in with the measurement-data role.
 *
 * Both sessions are pinned to \p host, so a datagram from any other sender is
 * discarded rather than taken for an answer. Neither session is left open on
 * failure.
 *
 * \retval KATHERINE_E_OK on success, with both transports valid.
 * \retval KATHERINE_E_INVAL if either transport is missing, or \p host is
 *   empty.
 * \retval KATHERINE_E_ADDRESS if \p host could not be resolved, or a local
 *   port could not be bound; see inet_pton(3) and bind(2).
 * \retval KATHERINE_E_NOMEM if a transport or its buffer could not be
 *   allocated.
 * \retval KATHERINE_E_IO if a socket call failed at the OS level for a reason
 *   none of the other codes name; see socket(2), setsockopt(2) and
 *   katherine_transport_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if a socket call reported that it would have
 *   blocked, which this backend renders as a timeout whatever raised it.
 * \retval KATHERINE_E_SYSTEM if a session's lock could not be created, or a
 *   socket call failed outside the backend's error map; see
 *   pthread_mutex_init(3).
 */
static inline katherine_error_t
katherine_transport_init_udp_pair(const char *host, bool should_change_udp_ports, katherine_transport_t *control, katherine_transport_t *data)
{
    katherine_error_t res;

    const katherine_udp_config_t control_session = {
        .local_port     = should_change_udp_ports ? 0 : KATHERINE_UDP_CONTROL_PORT,
        .remote_address = host,
        .remote_port    = KATHERINE_UDP_REMOTE_PORT,
        .timeout_ms     = KATHERINE_UDP_CONTROL_TIMEOUT,
        .pin_remote     = true,
    };

    const katherine_udp_config_t data_session = {
        .local_port     = should_change_udp_ports ? 0 : KATHERINE_UDP_DATA_PORT,
        .remote_address = host,
        .remote_port    = KATHERINE_UDP_REMOTE_PORT,
        .timeout_ms     = KATHERINE_UDP_DATA_TIMEOUT,
        .pin_remote     = true,
        .count_drops    = true,
    };

    if ((res = katherine_transport_init_udp(control, &control_session)) != KATHERINE_E_OK) {
        goto err_control;
    }

    if ((res = katherine_transport_init_udp(data, &data_session)) != KATHERINE_E_OK) {
        goto err_data;
    }

    return KATHERINE_E_OK;

err_data:
    katherine_transport_fini(control);
err_control:
    return res;
}

/**
 * Open both of a device's transports, or neither.
 * \param address Readout to open, naming its link.
 * \param should_change_udp_ports True to bind ports the OS picks rather than
 *   the standard pair, for a link with a port space. Ignored by links without
 *   one.
 * \param control Transport to fill in with the command role.
 * \param data Transport to fill in with the measurement-data role.
 *
 * The two roles cannot be opened separately on every link: USB claims one
 * interface and a second claim of it is refused, so its roles share what one
 * of them opened. Sharing is only safe while one caller builds both and one
 * caller takes both down, which is why there is no single-transport entry
 * point here and why katherine_transport_fini_pair() is the counterpart.
 *
 * Both transports are zeroed before anything is attempted, so a caller that
 * finalises after a refusal finalises nothing.
 *
 * \retval KATHERINE_E_OK on success, with both transports valid.
 * \retval KATHERINE_E_INVAL if an argument is missing.
 * \retval KATHERINE_E_ADDRESS if the address names no link this build carries,
 *   or the backend could not read the remainder as one of its own; see
 *   katherine_address_split() and katherine_transport_init_udp().
 * \retval KATHERINE_E_UNSUPPORTED if the address names a link whose two roles
 *   cannot yet be opened together.
 * \retval KATHERINE_E_NOMEM if a transport or its buffer could not be
 *   allocated.
 * \retval KATHERINE_E_IO if the backend's constructor failed at the OS level
 *   for a reason none of the other codes name; see
 *   katherine_transport_init_udp().
 * \retval KATHERINE_E_TIMEOUT if one of its calls reported that it would have
 *   blocked; see katherine_transport_init_udp().
 * \retval KATHERINE_E_SYSTEM if the session's lock could not be created, or
 *   the backend failed outside its own error map; see
 *   katherine_transport_init_udp().
 */
static inline katherine_error_t
katherine_transport_init_pair(const char *address, bool should_change_udp_ports, katherine_transport_t *control, katherine_transport_t *data)
{
    katherine_error_t res;

    if (address == NULL || control == NULL || data == NULL) {
        res = KATHERINE_E_INVAL;
        goto err_args;
    }

    // Zeroed first, so a caller that ignores the error code finalises nothing
    // rather than whatever its stack held.
    memset(control, 0, sizeof(*control));
    memset(data, 0, sizeof(*data));

    katherine_transport_kind_t kind;
    const char *rest;
    if ((res = katherine_address_split(address, &kind, &rest)) != KATHERINE_E_OK) {
        goto err_address;
    }

    switch (kind) {
    case KATHERINE_TRANSPORT_UDP:
        res = katherine_transport_init_udp_pair(rest, should_change_udp_ports, control, data);
        break;

    default:
        // A link whose two roles cannot yet be opened over one claim.
        res = KATHERINE_E_UNSUPPORTED;
        break;
    }

err_address:
err_args:
    return res;
}

/**
 * Close both transports of a pair and whatever they share.
 * \param control Control transport, which may be zeroed.
 * \param data Data transport, which may be zeroed.
 *
 * A zeroed transport is skipped, so a device assembled by hand rather than by
 * katherine_transport_init_pair() closes correctly too.
 */
static inline void
katherine_transport_fini_pair(katherine_transport_t *control, katherine_transport_t *data)
{
    // Reverse order of acquisition, and each only where there is one.
    if (data != NULL && data->ops != NULL) {
        katherine_transport_fini(data);
    }
    if (control != NULL && control->ops != NULL) {
        katherine_transport_fini(control);
    }
}

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
