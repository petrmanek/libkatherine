/**
 * \file
 * \brief The transport vtable, which only the library fills in.
 * \author Petr Mánek
 * \date 24.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <katherine/error.h>
#include <katherine/transport.h>

// Every operation taking a pointer to write through must tolerate NULL and
// succeed without writing, because katherine.h promises a caller that for
// the whole interface and these are reached through it.
typedef struct katherine_transport_ops {
#ifndef DOXYGEN_SHOULD_SKIP_THIS
    katherine_error_t (*send_cmd)(void *ctx, const void *data, size_t count);
    katherine_error_t (*recv_crd)(void *ctx, void *crd, size_t *len);
    katherine_error_t (*flush_incoming)(void *ctx, uint32_t *discarded);
    katherine_error_t (*acquire_data_buf)(void *ctx, void **buf, size_t *len);
    katherine_error_t (*release_data_buf)(void *ctx, void *buf);
    katherine_error_t (*set_timeout)(void *ctx, uint32_t timeout_ms);
    katherine_error_t (*get_dropped_input)(void *ctx, uint64_t *count);
    katherine_error_t (*mutex_lock)(void *ctx);
    katherine_error_t (*mutex_unlock)(void *ctx);
    int (*get_last_os_error)(void *ctx);
    void (*fini)(void *ctx);
#endif /* DOXYGEN_SHOULD_SKIP_THIS */
} katherine_transport_ops_t;
