/**
 * \file
 * \brief Finding readouts on a network.
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

// Must be the very first thing in the file, before any #include: monoclock.h
// guards its clock_gettime() behind _POSIX_C_SOURCE, and that only has an
// effect the first time any libc header pulls in <features.h> -- glibc
// resolves POSIX visibility once for the whole translation unit and does not
// revisit it. Harmless on Windows, whose headers do not gate on it.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <string.h>
#include <katherine/discover.h>
#include "monoclock.h"
#include "msleep.h"
#include "protocol/cmd_interface.h"

/** Port the readouts listen on, and the one a firmware that ignores the
 * reply port answers to. The discovery sends here and binds it, so that
 * those answers have somewhere to land. */
#define DISCOVER_DEFAULT_PORT 1555

/** Pause between rounds of the collection loop. */
#define DISCOVER_POLL_MS      1

/** One socket's result from a single poll. */
typedef enum {
    COLLECT_EMPTY,   ///< An empty socket.
    COLLECT_DISCARD, ///< Something was waiting, but it is not an answer.
    COLLECT_TAKEN,   ///< An answer, written to the entry.
} collect_result_t;

/**
 * Take one datagram from a socket, if one is waiting, and judge it.
 *
 * \param udp Session to poll
 * \param request The request as it was sent, for recognizing its own echo
 * \param legacy True for the socket bound to the standard port
 * \param entry Destination for an answer
 * \return What the poll found
 */
static collect_result_t
collect_one(katherine_udp_t *udp, const katherine_cmd_t *request, bool legacy, katherine_discovered_t *entry)
{
    char crd[KATHERINE_CMD_CRD_SIZE];
    size_t received = sizeof(crd);

    if (katherine_udp_recv_nowait(udp, crd, &received) != KATHERINE_E_OK) return COLLECT_EMPTY;
    if (received != KATHERINE_CMD_CRD_SIZE) return COLLECT_DISCARD;
    if ((uint8_t) crd[KATHERINE_CMD_OPCODE_BYTE] != CMD_TYPE_ECHO_CHIP_ID) return COLLECT_DISCARD;

    // The request itself, delivered back to its sender: a broadcast reaches
    // this host too, and a command and its response differ only in the
    // fields a readout fills in, so the operation code does not tell them
    // apart.
    if (memcmp(crd, request->b, sizeof(request->b)) == 0) return COLLECT_DISCARD;

    memset(entry, 0, sizeof(*entry));

    // TODO: once katherine_device_init() selects a transport from an address
    // prefix, this has to yield "udp://<address>" rather than the bare one.
    if (katherine_udp_get_remote_address(udp, entry->addr, sizeof(entry->addr)) != KATHERINE_E_OK) {
        return COLLECT_DISCARD;
    }
    entry->legacy = legacy;

    return COLLECT_TAKEN;
}

/**
 * Ask every readout reachable at an address to identify itself.
 *
 * Sends one echo-chip-id command and records the address of every readout
 * that answers. Aimed at a broadcast address it finds every readout on the
 * subnet; aimed at one host it is a liveness check for that host.
 *
 * Two sockets receive, and which one an answer arrives on is the firmware
 * generation. The request names \p reply_port in its first two bytes; a
 * firmware that reads it answers there, and one that does not answers on
 * the standard port, which this call binds for exactly that reason. The
 * second case sets katherine_discovered_t::legacy.
 *
 * \note The standard port is bound for the duration, so a discovery cannot
 *   run while a session on this host holds it -- which katherine_device_init()
 *   does for its control socket.
 *
 * \note The timeout bounds the call. Datagrams that are not answers are
 *   discarded, at most KATHERINE_CMD_MAX_STRAY_DISCARDS of them, so a port
 *   carrying other traffic cannot stretch it either. Choose it accordingly
 *   -- a subnet's worth of readouts answer within milliseconds of each
 *   other, so a few hundred milliseconds is generous.
 *
 * \note Duplicate answers are kept. A readout on a host with two addresses
 *   answers twice, from both, and which of them a caller should talk to is
 *   not this function's to decide.
 *
 * \param addr Address to ask, broadcast or unicast
 * \param reply_port Port a current firmware is to answer on; at least
 *   KATHERINE_DISCOVER_MIN_PORT, and not the standard port itself
 * \param timeout_ms Limit on the whole call, in milliseconds; at least one
 * \param out Storage for the answers, of at least the entries \p count gives
 * \param count On entry the capacity of \p out, on return the number of
 *   entries written, which is zero on every error
 *
 * \retval KATHERINE_E_OK on success, including when nothing answered.
 * \retval KATHERINE_E_INVAL if out or count is NULL, the capacity count
 *   gives is zero, the timeout is zero, the reply port is below
 *   KATHERINE_DISCOVER_MIN_PORT or is the standard port, or a socket call
 *   rejected an argument; see sendto(2) and socket(2).
 * \retval KATHERINE_E_ADDR if the address could not be parsed or either
 *   local port could not be bound; see inet_pton(3) and bind(2).
 * \retval KATHERINE_E_TIMEOUT if the request could not be sent because the
 *   send would have blocked; see sendto(2). Not returned for want of an
 *   answer -- a discovery that hears nothing succeeds with none found.
 * \retval KATHERINE_E_IO if a send or a socket setup call failed at the OS
 *   level for a reason none of the other codes cover.
 * \retval KATHERINE_E_NOMEM if a socket call ran out of memory.
 * \retval KATHERINE_E_SYSTEM if a session's lock could not be created.
 */
katherine_error_t
katherine_device_discover_udp(const char *addr, uint16_t reply_port, uint32_t timeout_ms, katherine_discovered_t *out, size_t *count)
{
    katherine_error_t res;

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
    if (reply_port < KATHERINE_DISCOVER_MIN_PORT || reply_port == DISCOVER_DEFAULT_PORT) {
        res = KATHERINE_E_INVAL;
        goto err_port;
    }
    // Zero is the one value the sessions below read as no timeout at all:
    // SO_RCVTIMEO is left unset and a receive then blocks past the deadline
    // this call keeps for itself. A millisecond is the shortest limit the
    // parameter can express, so anything under one is refused here.
    if (timeout_ms == 0) {
        res = KATHERINE_E_INVAL;
        goto err_timeout;
    }

    // Both unpinned on purpose: a pinned session would discard every answer
    // but the first host's, and adopting each sender in turn is how the
    // address of each answer is read back.
    //
    // The standard port is bound first because the request is sent from it.
    // A firmware that ignores the reply port answers either to its own
    // default or to the port the request came from, and binding the one the
    // request leaves from makes both of those the same socket.
    katherine_udp_t legacy;
    res = katherine_udp_init_bound(&legacy, NULL, DISCOVER_DEFAULT_PORT, addr, DISCOVER_DEFAULT_PORT, timeout_ms);
    if (res) goto err_legacy;

    katherine_udp_t modern;
    res = katherine_udp_init_bound(&modern, NULL, reply_port, addr, DISCOVER_DEFAULT_PORT, timeout_ms);
    if (res) goto err_modern;

    res = katherine_udp_set_broadcast(&legacy, true);
    if (res) goto err_broadcast;

    // The reply port travels in the payload, where the firmware reads it.
    katherine_cmd_t request = katherine_cmd_create(CMD_TYPE_ECHO_CHIP_ID);
    katherine_cmd_payload_set_i64(&request, (int64_t) reply_port);

    res = katherine_cmd_send(&legacy, request.b, sizeof(request.b));
    if (res) goto err_send;

    const uint64_t deadline = katherine_clock_monotonic_ns() + (uint64_t) timeout_ms * 1000000ULL;
    uint32_t discarded      = 0;

    while (*count < capacity && discarded < KATHERINE_CMD_MAX_STRAY_DISCARDS) {
        bool idle = true;

        for (size_t i = 0; i < 2 && *count < capacity; ++i) {
            const bool is_legacy = (i == 0);
            katherine_udp_t *udp = is_legacy ? &legacy : &modern;

            switch (collect_one(udp, &request, is_legacy, &out[*count])) {
            case COLLECT_TAKEN:
                ++*count;
                idle = false;
                break;
            case COLLECT_DISCARD:
                ++discarded;
                idle = false;
                break;
            case COLLECT_EMPTY:
                break;
            }
        }

        if (idle) {
            if (katherine_clock_monotonic_ns() >= deadline) break;
            katherine_msleep(DISCOVER_POLL_MS);
        }
    }

    katherine_udp_fini(&modern);
    katherine_udp_fini(&legacy);
    return KATHERINE_E_OK;

err_send:
err_broadcast:
    katherine_udp_fini(&modern);
err_modern:
    katherine_udp_fini(&legacy);
err_legacy:
err_timeout:
err_port:
err_capacity:
err_count:
err_out:
    return res;
}
