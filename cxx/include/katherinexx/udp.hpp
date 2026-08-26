/**
 * @file
 * @brief C++ wrapper for a UDP communication session.
 * @author Petr Mánek
 * @date 24.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <katherine/udp.h>

#include <katherinexx/error.hpp>

namespace katherine {

/**
 * @addtogroup cxx_api
 * @{
 */

/* Note: katherine_udp_mutex_lock()/katherine_udp_mutex_unlock() are
   deliberately not wrapped here. They exist to let a caller hold the
   session's mutex across several C API calls that must observe it as one
   unit (e.g. a send followed by the matching receive); a lock/unlock pair
   with nothing in between them would not provide that, and a scoped-lock
   wrapper would either have to expose the raw katherine_udp_t (defeating
   the point of this class) or accept an arbitrary callback, which is more
   machinery than this thin session wrapper is meant to carry. Callers who
   need the mutex reach it through c_udp(). */
class udp {
    katherine_udp_t udp_;

public:
    udp(std::string local_addr, std::uint16_t local_port, std::string remote_addr, std::uint16_t remote_port, std::uint32_t timeout_ms)
    {
        int res = katherine_udp_init_bound(&udp_, local_addr.empty() ? nullptr : local_addr.c_str(), local_port, remote_addr.c_str(), remote_port, timeout_ms);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    udp(const udp&)            = delete;
    udp& operator=(const udp&) = delete;

    virtual ~udp()
    {
        katherine_udp_fini(&udp_);
    }

    katherine_udp_t *c_udp() { return &udp_; }
    const katherine_udp_t *c_udp() const { return &udp_; }

    void
    send_exact(const void *data, std::size_t count)
    {
        int res = katherine_udp_send_exact(&udp_, data, count);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    recv_exact(void *data, std::size_t count)
    {
        int res = katherine_udp_recv_exact(&udp_, data, count);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    /* count is in/out, exactly as in the C call: it carries the buffer
       capacity in and the number of bytes actually received out. */
    void
    recv(void *data, std::size_t& count)
    {
        int res = katherine_udp_recv(&udp_, data, &count);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    /**
     * Receive one datagram without waiting for one to arrive.
     * @param data Inbound buffer start
     * @param count Buffer capacity in, bytes received out, exactly as in the C call
     * @throws katherine::system_error -KATHERINE_E_TIMEOUT if nothing is queued, rather
     *   than waiting for the session's receive timeout
     */
    void
    recv_nowait(void *data, std::size_t& count)
    {
        int res = katherine_udp_recv_nowait(&udp_, data, &count);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    set_remote(std::string remote_addr, std::uint16_t remote_port)
    {
        int res = katherine_udp_set_remote(&udp_, remote_addr.c_str(), remote_port);

        if (res != 0) {
            throw katherine::system_error{res};
        }
    }

    void
    pin_remote()
    {
        katherine_udp_pin_remote(&udp_);
    }

    /**
     * Require command responses to repeat the operation code of their request exactly.
     * @param strict True to require the request's own operation code, false (the default)
     *   to accept the readout firmware's documented substitutions as well; see
     *   katherine_udp_set_strict_ack() for what those are and why they are tolerated
     */
    void
    set_strict_ack(bool strict)
    {
        katherine_udp_set_strict_ack(&udp_, strict);
    }

    /**
     * Count command responses this session discarded as belonging to no request of its own.
     * @return The running count since the session was created; see
     *   katherine_udp_set_strict_ack()
     */
    std::uint64_t stray_command_responses() const { return udp_.stray_command_responses; }
};

/** @} */

}
