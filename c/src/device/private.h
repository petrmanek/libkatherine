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

#include <assert.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include <katherine/device.h>
#include <katherine/transport.h>

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

/**
 * Take the control link's lock, refusing a device that has no link.
 *
 * katherine_device_init() zeroes the device before the first step that can
 * fail, so a caller that ignores its error and carries on holds one whose
 * every transport operation dereferences a null vtable. Twenty-one entry
 * points reached that dereference, some of them only after
 * katherine_device_declare() had satisfied the
 * katherine_device_derived_info_t::supported guard they relied on. Every
 * control-plane exchange starts here, which is the one place that covers
 * them and the next one written.
 */
static inline katherine_error_t
katherine_device_control_lock(katherine_device_t *device)
{
    if (device->control_transport.ops == NULL) return KATHERINE_E_STATE;
    return katherine_transport_mutex_lock(&device->control_transport);
}

/**
 * Release the lock katherine_device_control_lock() took.
 *
 * Paired with it by name so that a control-plane function can be read for
 * balance without resolving which transport each side names: every lock in
 * c/src/device is one of these and every unlock is the other, and a call to
 * katherine_transport_mutex_unlock() there is now a defect by construction.
 *
 * Takes no NULL-ops guard. Reaching an unlock means the matching lock
 * returned KATHERINE_E_OK, which it cannot have done on a device with no
 * link.
 */
static inline katherine_error_t
katherine_device_control_unlock(katherine_device_t *device)
{
    return katherine_transport_mutex_unlock(&device->control_transport);
}

/**
 * The readout generation, which may only be asked for once the device is
 * known.
 *
 * A device that has been neither enumerated nor declared carries a zeroed
 * derived record, where gen reads 0 -- indistinguishable from a genuine
 * first-generation readout. Every caller therefore tests
 * katherine_device_derived_info_t::supported first, and nine of them do.
 *
 * This is where the tenth is caught. The assertion costs nothing in a
 * release build and fails the Debug ones CI runs under both sanitizers, so
 * a forgotten guard stops being a readout silently driven as the wrong
 * generation and becomes a test that aborts.
 */
static inline uint8_t
katherine_device_gen(const katherine_device_t *device)
{
    assert(device->derived_info.supported);
    return device->derived_info.gen;
}
