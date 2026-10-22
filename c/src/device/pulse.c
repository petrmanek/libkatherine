/**
 * \file
 * \brief The readout's pulse generator.
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <katherine/pulse.h>
#include <katherine/device.h>
#include "protocol/cmd_interface.h"

/** Names of the outputs, in enumeration order. */
static const char *const KATHERINE_PULSE_OUTPUT_NAME[] = {
    // clang-format off
    "pulse",
    "pulse_inverted",
    "passthrough",
    "passthrough_inverted",
    // clang-format on
};

/** Names of the trigger sources, in enumeration order. */
static const char *const KATHERINE_PULSE_SOURCE_NAME[] = {
    // clang-format off
    "rising_edge",
    "falling_edge",
    "oscillator",
    // clang-format on
};

/**
 * Name of a pulse channel's output.
 * \param output Output to name
 * \return Its name, or "unknown" outside the enumeration.
 */
const char *
katherine_pulse_output_str(katherine_pulse_output_t output)
{
    const size_t n = sizeof(KATHERINE_PULSE_OUTPUT_NAME) / sizeof(KATHERINE_PULSE_OUTPUT_NAME[0]);

    if ((size_t) output >= n) return "unknown";

    return KATHERINE_PULSE_OUTPUT_NAME[output];
}

/**
 * Name of a trigger source.
 * \param source Source to name
 * \return Its name, or "unknown" outside the enumeration.
 */
const char *
katherine_pulse_source_str(katherine_pulse_source_t source)
{
    const size_t n = sizeof(KATHERINE_PULSE_SOURCE_NAME) / sizeof(KATHERINE_PULSE_SOURCE_NAME[0]);

    if ((size_t) source >= n) return "unknown";

    return KATHERINE_PULSE_SOURCE_NAME[source];
}

/**
 * Set up the readout's pulse generator.
 *
 * Each channel produces one pulse from every trigger, or carries the trigger
 * through. What reaches a connector is a GPIO channel mapped onto
 * KATHERINE_GPIO_PULSE_INTERNAL or one of the pulse channels; channels 1 to 3
 * also have outputs of their own, gated by
 * katherine_pulse_channel_config_t::enabled.
 *
 * Channels this readout does not have take their defaults, whatever \p config
 * says for them. Lengths and delays are honoured to 10 ns or better, and a
 * delay longer than the readout can hold is refused rather than truncated.
 *
 * \param device Katherine device
 * \param config Configuration to apply
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared.
 * \retval KATHERINE_E_UNSUPPORTED if the readout is of the first generation,
 *   whose firmware does not carry this operation code.
 * \retval KATHERINE_E_INVAL if an output or a source is outside its
 *   enumeration, a length or a delay is negative, not a number or longer than
 *   the readout can hold, an oscillator rate is one it cannot stand in at, or
 *   the control link rejected an argument; see
 *   katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge a write
 *   within the control link's receive timeout.
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
katherine_set_pulse_generator(katherine_device_t *device, const katherine_pulse_config_t *config)
{
    katherine_error_t res;

    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_enumerated;
    }
    if (device->derived_info.gen < 2) {
        res = KATHERINE_E_UNSUPPORTED;
        goto err_generation;
    }

    // How the generator's two composed words are laid out, which is this
    // function's business and nobody else's: every one of these is used here
    // and nowhere in the library.
    static const uint8_t bit_oscillator = 30; // in the enable word
    static const uint8_t bit_enable     = 31;
    static const uint8_t bit_only_ready = 8; // in the setup word
    static const uint8_t bit_edge       = 31;
    static const uint8_t output_bits    = 2; // per channel, in the setup word
    static const double tick_ns         = 10.0;
    static const double oscillator_hz   = 50e6;
    static const uint32_t delay_max     = 0xFFFFFFu; // the register is narrower than its word

    if (config->source > KATHERINE_PULSE_SOURCE_OSCILLATOR) {
        res = KATHERINE_E_INVAL;
        goto err_source;
    }

    const uint8_t have = device->derived_info.pulse_channel_count;

    uint32_t setup                                       = 0;
    uint32_t enable                                      = 0;
    uint32_t ticks[KATHERINE_MAX_PULSE_CHANNEL_COUNT][2] = {{0}};

    for (uint8_t i = 0; i < KATHERINE_MAX_PULSE_CHANNEL_COUNT; ++i) {
        const katherine_pulse_channel_config_t *ch = &config->channel[i];

        if (i >= have) break;

        if (ch->output > KATHERINE_PULSE_OUTPUT_PASSTHROUGH_INVERTED) {
            res = KATHERINE_E_INVAL;
            goto err_channel;
        }

        const double length_f = ch->length_seconds * 1e9 / tick_ns;
        const double delay_f  = ch->delay_seconds * 1e9 / tick_ns;

        if (!(length_f >= 0.0 && length_f <= (double) UINT32_MAX) || !(delay_f >= 0.0 && delay_f <= (double) delay_max)) {
            res = KATHERINE_E_INVAL;
            goto err_channel;
        }

        ticks[i][0] = (uint32_t) (length_f + 0.5);
        ticks[i][1] = (uint32_t) (delay_f + 0.5);

        setup |= (uint32_t) ch->output << (i * output_bits);

        if (!ch->enabled) continue;

        enable |= UINT32_C(1) << bit_enable;

        // The first channel is internal and has no output gate of its own;
        // the other three occupy the low bits of the enable word.
        if (i > 0) enable |= UINT32_C(1) << (i - 1);
    }

    if (config->only_when_ready) setup |= UINT32_C(1) << bit_only_ready;
    if (config->source == KATHERINE_PULSE_SOURCE_FALLING_EDGE) setup |= UINT32_C(1) << bit_edge;

    uint32_t divisor = 1;

    if (config->source == KATHERINE_PULSE_SOURCE_OSCILLATOR) {
        const double d = oscillator_hz / config->oscillator_frequency_hz;

        if (!(d >= 1.0 && d <= (double) UINT32_MAX)) {
            res = KATHERINE_E_INVAL;
            goto err_oscillator;
        }

        divisor = (uint32_t) (d + 0.5);
        enable |= UINT32_C(1) << bit_oscillator;
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_pulse_generator_write(&device->control_transport, KATHERINE_PULSE_REG_SETUP, setup);
    if (res) goto err_setup;

    for (uint8_t i = 0; i < have; ++i) {
        const katherine_pulse_reg_t reg = (katherine_pulse_reg_t) (KATHERINE_PULSE_REG_LENGTH_0 + 2 * i);

        res = katherine_cmd_pulse_generator_write(&device->control_transport, reg, ticks[i][0]);
        if (res) goto err_length;

        res = katherine_cmd_pulse_generator_write(&device->control_transport, (katherine_pulse_reg_t) (reg + 1), ticks[i][1]);
        if (res) goto err_delay;
    }

    res = katherine_cmd_pulse_generator_write(&device->control_transport, KATHERINE_PULSE_REG_OSC_DIVISOR, divisor);
    if (res) goto err_divisor;

    res = katherine_cmd_pulse_generator_write(&device->control_transport, KATHERINE_PULSE_REG_ENABLE, enable);
    if (res) goto err_enable;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_enable:
err_divisor:
err_delay:
err_length:
err_setup:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_oscillator:
err_channel:
err_source:
err_generation:
err_enumerated:
    return res;
}

/**
 * Read the generator's measurements of its trigger.
 *
 * Fills in how long the trigger last spent low and high, how many rising
 * edges have arrived since katherine_reset_pulse_generator_counts(), and the
 * frequency of the generator's own clock.
 *
 * \param device Katherine device
 * \param counts Measurements to fill
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared.
 * \retval KATHERINE_E_UNSUPPORTED if the readout is of the first generation,
 *   whose firmware does not carry this operation code.
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer a read within the
 *   control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response was not the length of a command
 *   response.
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
katherine_get_pulse_generator_counts(katherine_device_t *device, katherine_pulse_counts_t *counts)
{
    static const katherine_pulse_reg_t REGS[] = {
        KATHERINE_PULSE_REG_LOW_TIME,
        KATHERINE_PULSE_REG_HIGH_TIME,
        KATHERINE_PULSE_REG_EDGE_COUNT,
        KATHERINE_PULSE_REG_CLOCK,
    };
    // The intervals are converted with the ten-nanosecond tick rather than
    // with the clock reported alongside them. The two agree on every readout
    // measured so far -- a reported 100 MHz is that same tick -- and a
    // readout answering zero would otherwise make the conversion meaningless.
    // The reported clock is handed back unconverted so a caller can check.
    static const double tick_ns = 10.0;

    katherine_error_t res;
    uint32_t raw[4] = {0};

    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_enumerated;
    }
    if (device->derived_info.gen < 2) {
        res = KATHERINE_E_UNSUPPORTED;
        goto err_generation;
    }

    memset(counts, 0, sizeof(*counts));

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    for (uint8_t i = 0; i < sizeof(REGS) / sizeof(REGS[0]); ++i) {
        res = katherine_cmd_pulse_generator_read(&device->control_transport, REGS[i], &raw[i]);
        if (res) goto err_read;
    }

    counts->low_seconds  = (double) raw[0] * tick_ns * 1e-9;
    counts->high_seconds = (double) raw[1] * tick_ns * 1e-9;
    counts->rising_edges = raw[2];
    counts->clock_mhz    = raw[3];

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_read:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_generation:
err_enumerated:
    return res;
}

/**
 * Reset the generator's count of rising edges on its trigger.
 *
 * The count is reset by writing zero to the register it is read from;
 * any other value leaves it alone, which is why this takes no argument.
 *
 * \param device Katherine device
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared.
 * \retval KATHERINE_E_UNSUPPORTED if the readout is of the first generation,
 *   whose firmware does not carry this operation code.
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge the write
 *   within the control link's receive timeout.
 * \retval KATHERINE_E_CLOSED if the link went away mid-exchange, which a
 *   removable one can do and a socket cannot: a USB readout unplugged or
 *   reset out from under the session; see katherine_transport_get_last_os_error().
 * \retval KATHERINE_E_BAD_CRD if a response was not the length of a command
 *   response.
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
katherine_reset_pulse_generator_counts(katherine_device_t *device)
{
    katherine_error_t res;

    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_enumerated;
    }
    if (device->derived_info.gen < 2) {
        res = KATHERINE_E_UNSUPPORTED;
        goto err_generation;
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_pulse_generator_write(&device->control_transport, KATHERINE_PULSE_REG_EDGE_COUNT, 0);
    if (res) goto err_write;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_write:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_generation:
err_enumerated:
    return res;
}
