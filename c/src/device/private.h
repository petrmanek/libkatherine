/**
 * \file
 * \brief What the library keeps about an open device.
 * \author Petr Mánek
 * \date 24.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include <katherine/device.h>

/**
 * A device's private state.
 *
 * Lives in katherine_device_t::priv. Declared whole whatever the library was
 * built with: a device is not specialised by transport, and gating two
 * members on KATHERINE_FEATURE_UDP is what used to make the public structure
 * a different size in a build without UDP.
 */
typedef struct katherine_device_state {
    uint16_t udp_data_port; ///< Port the readout was told to send measurement data to, which an acquisition binds. Meaningless on a link that is not udp://.
    bool udp_ports_changed; ///< True if this device moved the readout off the standard UDP ports, which is how katherine_device_fini() knows to put it back. Meaningless on a link that is not udp://.
} katherine_device_state_t;

_Static_assert(sizeof(katherine_device_state_t) <= KATHERINE_DEVICE_PRIV_SIZE,
    "the device's private state must fit katherine_device_t::priv");
_Static_assert(alignof(katherine_device_state_t) <= alignof(katherine_device_priv_t),
    "katherine_device_t::priv must be aligned at least as strictly as what it holds");

/** The private state of a device. */
static inline katherine_device_state_t *
katherine_device_state(katherine_device_t *device)
{
    return (katherine_device_state_t *) &device->priv;
}

/** \copybrief katherine_device_state() */
static inline const katherine_device_state_t *
katherine_device_state_const(const katherine_device_t *device)
{
    return (const katherine_device_state_t *) &device->priv;
}
