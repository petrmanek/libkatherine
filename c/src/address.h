/**
 * \file
 * \brief Internal address scheme handling.
 * \author Petr Mánek
 * \date 18.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <katherine/error.h>
#include <katherine/global.h>
#include <katherine/transport.h>

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#ifdef __cplusplus
extern "C" {
#endif

// An address names a link as well as a readout on it: "udp://192.168.1.42"
// rather than "192.168.1.42". The scheme token is the one
// katherine_str_transport_kind() renders, so the two cannot drift.
//
// The part after the separator is the transport's own, and this layer does
// not look at it. A UDP transport reads a host there; a USB transport reads
// a bus and a chain of port numbers.

// Separates the scheme from the address the transport understands.
#define KATHERINE_ADDRESS_SCHEME_SEP "://"

/**
 * Split an address into the link it names and the rest, which is the named
 * transport's to parse. Points \p rest into \p address rather than copying.
 *
 * An address with no scheme is a UDP address: that is the only shape there
 * was before 2.0, and every one of them names a readout on a network.
 */
static inline katherine_error_t
katherine_address_split(const char *address, katherine_transport_kind_t *kind, const char **rest)
{
    // Every scheme an address may name. The tokens come from
    // katherine_str_transport_kind(), so only this list is kept in step.
    static const katherine_transport_kind_t KINDS[] = {KATHERINE_TRANSPORT_UDP, KATHERINE_TRANSPORT_USB};

    katherine_error_t res;
    katherine_transport_kind_t found = KATHERINE_TRANSPORT_UDP;
    const char *body                 = address;

    if (address == NULL) {
        res = KATHERINE_E_ADDRESS;
        goto err_null;
    }

    const char *sep = strstr(address, KATHERINE_ADDRESS_SCHEME_SEP);
    if (sep != NULL) {
        const size_t len = (size_t) (sep - address);
        bool named       = false;

        for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); ++i) {
            const char *token = katherine_str_transport_kind(KINDS[i]);

            if (strlen(token) == len && memcmp(address, token, len) == 0) {
                found = KINDS[i];
                named = true;
                break;
            }
        }

        if (!named) {
            res = KATHERINE_E_ADDRESS;
            goto err_scheme;
        }

        body = sep + strlen(KATHERINE_ADDRESS_SCHEME_SEP);
    }

    // A scheme and nothing else names no readout.
    if (*body == '\0') {
        res = KATHERINE_E_ADDRESS;
        goto err_empty;
    }

    *kind = found;
    *rest = body;

    return KATHERINE_E_OK;

err_empty:
err_scheme:
err_null:
    return res;
}

/**
 * Write the address naming \p rest on a link of \p kind, in the form
 * katherine_address_split() reads back.
 *
 * Returns what snprintf(3) returns, so a value of \p cap or more says the
 * address was truncated.
 */
static inline int
katherine_address_format(char *buf, size_t cap, katherine_transport_kind_t kind, const char *rest)
{
    return snprintf(buf, cap, "%s" KATHERINE_ADDRESS_SCHEME_SEP "%s", katherine_str_transport_kind(kind), rest);
}

#ifdef __cplusplus
}
#endif

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
