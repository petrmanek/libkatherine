/**
 * \file
 * \brief Naming and rendering of the GPIO channels.
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <katherine/gpio.h>
#include <katherine/device.h>
#include "protocol/cmd_interface.h"

// A uint8_t counter walking this bound terminates only while the bound fits
// in one. KATHERINE_TPX3_MATRIX_WIDTH is 256 and takes a uint16_t counter for
// exactly this reason: an eight-bit one is always below it and the loop never
// ends. Stated here because the compiler is silent about it either way.
_Static_assert(KATHERINE_MAX_GPIO_COUNT <= 255, "KATHERINE_MAX_GPIO_COUNT must fit the uint8_t counters that walk it");

/**
 * Names of the mappings, in enumeration order. Their meanings are on the
 * enumerators; this is only the spelling.
 */
static const char *const KATHERINE_GPIO_MAPPING_NAME[] = {
    // clang-format off
    "input",
    "low",
    "pulse_internal",
    "pulse_channel_0",
    "pulse_channel_1",
    "pulse_channel_2",
    "level_0",
    "level_1",
    "level_2",
    "level_3",
    "level_4",
    "level_5",
    "level_6",
    "level_7",
    "ready_local",
    "ready_composed",
    "event_trigger",
    "chip_0_pixel_data",
    "chip_1_pixel_data",
    "chip_2_pixel_data",
    "chip_3_pixel_data",
    "shutter",
    "cascade_chain",
    "clock",
    "sync_signal",
    "spare_0",
    "spare_1",
    "spare_2",
    "spare_3",
    "spare_4",
    "spare_5",
    "spare_6",
    "high",
    // clang-format on
};

/**
 * Name of a channel mapping.
 * \param mapping Mapping to name
 * \return Its name, or "unknown" outside the enumeration.
 */
const char *
katherine_str_gpio_mapping(katherine_gpio_mapping_t mapping)
{
    const size_t n = sizeof(KATHERINE_GPIO_MAPPING_NAME) / sizeof(KATHERINE_GPIO_MAPPING_NAME[0]);

    if ((unsigned) mapping >= n) return "unknown";

    return KATHERINE_GPIO_MAPPING_NAME[mapping];
}

/**
 * Name of what a channel's level is doing.
 * \param signal Signal state to name
 * \return Its name, or "unknown" outside the enumeration.
 */
const char *
katherine_str_gpio_monitor_signal(katherine_gpio_monitor_signal_t signal)
{
    switch (signal) {
    case KATHERINE_GPIO_MONITOR_SIGNAL_UNKNOWN:  return "unknown";
    case KATHERINE_GPIO_MONITOR_SIGNAL_CONSTANT: return "constant";
    case KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC: return "periodic";
    }

    return "unknown";
}

/**
 * Frequency of a channel's signal.
 *
 * A channel earns its periodic classification from edges alone, so this is a
 * frequency only where the signal repeats.
 *
 * \param v Channel state, as katherine_probe_gpio() filled it
 * \return The frequency in Hertz, or zero where the readout measured no period
 *   -- a constant level, a channel it does not time, or a signal faster than
 *   one tick, which leaves both counters at zero.
 */
double
katherine_gpio_channel_frequency(const katherine_gpio_channel_monitor_t *v)
{
    const uint64_t period = (uint64_t) v->high_ticks + v->low_ticks;

    if (v->signal != KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC || period == 0) return 0.0;

    return 1e9 / ((double) period * KATHERINE_GPIO_TICK_NS);
}

/**
 * Fraction of a channel's period spent high.
 *
 * A channel earns its periodic classification from edges alone, so this is a
 * duty cycle only where the signal repeats.
 *
 * \param v Channel state, as katherine_probe_gpio() filled it
 * \return The fraction, between 0 and 1, or zero where the readout measured no
 *   period.
 */
double
katherine_gpio_channel_duty_cycle(const katherine_gpio_channel_monitor_t *v)
{
    const uint64_t period = (uint64_t) v->high_ticks + v->low_ticks;

    if (v->signal != KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC || period == 0) return 0.0;

    return (double) v->high_ticks / (double) period;
}

/** Presence of a channel on this readout. */
static bool
gpio_present(const katherine_device_t *device, uint8_t channel)
{
    return channel < device->derived_info.all_gpio_count;
}

/**
 * Ability of a channel to terminate its line, which not every channel has:
 * the two counts differ because not every channel reaches a connector.
 */
static bool
gpio_terminable(const katherine_device_t *device, uint8_t channel)
{
    return channel < device->derived_info.accessible_gpio_count;
}

/**
 * Set every GPIO channel's direction, function and termination.
 *
 * The composed ready signal is not applied. That is the work of
 * katherine_set_composed_ready_signal(), which reads the ready fields this call
 * ignores, so one configuration serves both.
 *
 * Channels this readout does not have take their defaults, whatever \p config
 * says for them. Termination of a channel without a connector is refused.
 *
 * \param device Katherine device
 * \param config Configuration to apply
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, so its channel count is unknown.
 * \retval KATHERINE_E_INVAL if a mapping is outside the enumeration, or a
 *   channel without a connector is to be terminated, or the control link
 *   rejected an argument; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge a write
 *   within the control link's receive timeout, which is the answer while an
 *   acquisition is armed or running.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response was not the length of a command
 *   response.
 * \retval KATHERINE_E_STRAY if an acknowledgement never arrived while
 *   responses to other commands kept coming.
 * \retval KATHERINE_E_IO if a send or receive failed at the OS level for a
 *   reason none of the other codes cover.
 * \retval KATHERINE_E_NOMEM if a send or receive ran out of memory.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_set_gpio_config(katherine_device_t *device, const katherine_gpio_config_t *config)
{
    katherine_error_t res;

    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_enumerated;
    }

    // An input reads; anything else drives, carrying the mapping one below
    // the value that named it, which is what leaves room for the input at
    // zero. A channel this readout does not have takes its default here,
    // whatever the caller asked for, and the numbering is contiguous, so the
    // first absent channel ends the loop.
    uint32_t direction                     = 0;
    uint32_t terminations                  = 0;
    uint32_t mux[KATHERINE_MAX_GPIO_COUNT] = {0};

    for (uint8_t i = 0; i < KATHERINE_MAX_GPIO_COUNT; ++i) {
        const katherine_gpio_channel_config_t *ch = &config->channel[i];

        if (!gpio_present(device, i)) break;

        if (ch->mapping > KATHERINE_GPIO_HIGH || (ch->termination && !gpio_terminable(device, i))) {
            res = KATHERINE_E_INVAL;
            goto err_check;
        }

        if (ch->mapping != KATHERINE_GPIO_INPUT) {
            direction |= 1u << i;
            mux[i] = (uint32_t) ch->mapping - 1;
        }

        if (ch->termination) terminations |= 1u << i;
    }

    uint32_t low = direction << 8;
    low |= mux[0] << 16;
    low |= mux[1] << (16 + KATHERINE_ACQ_UNIT_GPIO_MUX_BITS);

    uint32_t high = 0;
    for (uint8_t i = 2; i < KATHERINE_MAX_GPIO_COUNT; ++i) {
        high |= mux[i] << ((i - 2) * KATHERINE_ACQ_UNIT_GPIO_MUX_BITS);
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_acq_unit_write(&device->control_transport, KATHERINE_ACQ_UNIT_REG_GPIO_DIRECTION_SETUP, low);
    if (res) goto err_direction;

    res = katherine_cmd_acq_unit_write(&device->control_transport, KATHERINE_ACQ_UNIT_REG_MULTIPLEXER_SETUP, high);
    if (res) goto err_mux;

    res = katherine_cmd_acq_unit_write(&device->control_transport, KATHERINE_ACQ_UNIT_REG_GPIO_TERMINATION_EN, terminations);
    if (res) goto err_termination;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_termination:
err_mux:
err_direction:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_check:
err_enumerated:
    return res;
}

/**
 * Configure the role of GPIO channels in the generation of the composed
 * ready signal.
 *
 * The local ready signal goes high while the readout is armed and waiting
 * for its start trigger. The composed signal adds the channels named here,
 * each required to be high or to be low. A configuration naming no channel
 * makes the two identical.
 *
 * Only the ready fields of \p config are read, so one configuration serves
 * this call and katherine_set_gpio_config(). Channels this readout does not
 * have take no part, whatever \p config says for them.
 *
 * \param device Katherine device
 * \param config Configuration to apply
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, so its channel count is unknown.
 * \retval KATHERINE_E_INVAL if a role is outside the enumeration, or the
 *   control link rejected an argument; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge within the
 *   control link's receive timeout, which is the answer while an acquisition
 *   is armed or running.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response was not the length of a command
 *   response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while
 *   responses to other commands kept coming.
 * \retval KATHERINE_E_IO if the send or receive failed at the OS level for a
 *   reason none of the other codes cover.
 * \retval KATHERINE_E_NOMEM if the send or receive ran out of memory.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_set_composed_ready_signal(katherine_device_t *device, const katherine_gpio_config_t *config)
{
    katherine_error_t res;

    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_enumerated;
    }

    uint32_t word = 0;

    for (uint8_t i = 0; i < KATHERINE_MAX_GPIO_COUNT; ++i) {
        if (!gpio_present(device, i)) break;

        if (config->channel[i].ready > KATHERINE_GPIO_READY_WHEN_LOW) {
            res = KATHERINE_E_INVAL;
            goto err_check;
        }

        switch (config->channel[i].ready) {
        case KATHERINE_GPIO_READY_WHEN_HIGH: word |= 1u << i; break;
        case KATHERINE_GPIO_READY_WHEN_LOW:  word |= 1u << (i + 8); break;
        case KATHERINE_GPIO_READY_UNUSED:    break;
        }
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_acq_unit_write(&device->control_transport, KATHERINE_ACQ_UNIT_REG_READY_COMPLETE_SETUP, word);
    if (res) goto err_write;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_write:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_check:
err_enumerated:
    return res;
}

/**
 * Read the level and the signal classification of every GPIO channel.
 *
 * Every channel reports its level. A readout times only some of its
 * channels, and those also report a classification and the ticks their level
 * last spent high and low; the rest report
 * KATHERINE_GPIO_MONITOR_SIGNAL_UNKNOWN.
 *
 * A channel without an edge within the readout's window of about five
 * seconds is KATHERINE_GPIO_MONITOR_SIGNAL_CONSTANT, and its counts stay at
 * zero rather than a stale period.
 *
 * The classification follows from edges alone, so a single pulse already
 * makes a channel KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC and the counts then
 * describe that one pulse. katherine_gpio_channel_frequency() is a frequency
 * only where the signal really repeats.
 *
 * \param device Katherine device
 * \param state Channel states to fill, or NULL to make the reads and discard
 *   what they returned
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, so its channel count is unknown.
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer a read within the
 *   control link's receive timeout, which is the answer while an acquisition
 *   is armed or running.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response was not the length of a command
 *   response, or answered for another register.
 * \retval KATHERINE_E_STRAY if an answer never arrived while responses to
 *   other commands kept coming.
 * \retval KATHERINE_E_INVAL if the control link rejected an argument; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_IO if a send or receive failed at the OS level for a
 *   reason none of the other codes cover.
 * \retval KATHERINE_E_NOMEM if a send or receive ran out of memory.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be taken;
 *   see katherine_transport_get_last_os_error().
 */
katherine_error_t
katherine_probe_gpio(katherine_device_t *device, katherine_gpio_monitor_t *state)
{
    katherine_error_t res;

    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_enumerated;
    }

    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    uint32_t word;
    res = katherine_cmd_acq_unit_read(&device->control_transport, KATHERINE_ACQ_UNIT_REG_GPIO_DIRECTION_SETUP, &word);
    if (res) goto err_levels;

    // Levels in the low byte, one bit per channel; the validity of the tick
    // counters in the top four bits, one per timed channel.
    const uint32_t levels = word & 0xFFu;
    const uint32_t timed  = word >> 28;

    for (uint8_t i = 0; i < KATHERINE_MAX_GPIO_COUNT; ++i) {
        if (state != NULL) state->channel[i].level = (levels & (1u << i)) != 0;

        if (i >= KATHERINE_ACQ_UNIT_GPIO_TIMED_CHANNELS) continue;

        if ((timed & (1u << i)) == 0) {
            if (state != NULL) state->channel[i].signal = KATHERINE_GPIO_MONITOR_SIGNAL_CONSTANT;
            continue;
        }

        if (state != NULL) state->channel[i].signal = KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC;

        uint32_t high_ticks, low_ticks;

        res = katherine_cmd_acq_unit_read(&device->control_transport, (katherine_acq_unit_reg_t) (KATHERINE_ACQ_UNIT_REG_GPI_HIGH_TIME_0 + 2 * i), &high_ticks);
        if (res) goto err_ticks;

        res = katherine_cmd_acq_unit_read(&device->control_transport, (katherine_acq_unit_reg_t) (KATHERINE_ACQ_UNIT_REG_GPI_HIGH_TIME_0 + 2 * i + 1), &low_ticks);
        if (res) goto err_ticks;

        if (state != NULL) {
            state->channel[i].high_ticks = high_ticks;
            state->channel[i].low_ticks  = low_ticks;
        }
    }

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_ticks:
err_levels:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_enumerated:
    return res;
}
