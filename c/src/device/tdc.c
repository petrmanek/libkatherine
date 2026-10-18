/**
 * \file
 * \brief The readout's timing unit.
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <katherine/tdc.h>
#include <katherine/device.h>
#include "protocol/cmd_interface.h"

/**
 * Names of the sources, in enumeration order.
 */
static const char *const KATHERINE_TDC_SOURCE_NAME[] = {
    // clang-format off
    "gpio_0",
    "gpio_1",
    "gpio_2",
    "gpio_3",
    "chip_0_feedback",
    "chip_1_feedback",
    "chip_2_feedback",
    "chip_3_feedback",
    // clang-format on
};

/**
 * Name of a timing channel's source.
 * \param source Source to name
 * \return Its name, or "unknown" outside the enumeration.
 */
const char *
katherine_tdc_source_str(katherine_tdc_source_t source)
{
    const size_t n = sizeof(KATHERINE_TDC_SOURCE_NAME) / sizeof(KATHERINE_TDC_SOURCE_NAME[0]);

    if ((unsigned) source >= n) return "unknown";

    return KATHERINE_TDC_SOURCE_NAME[source];
}

/**
 * Set every timing channel's source, edge and participation.
 *
 * Since the number of channels differs depending on device model, only the
 * number of channels for the current device (retrieved from
 * katherine_device_derived_info_t::tdc_channel_count) is meaningfully
 * interpreted. The remaining channels in \p config are disregarded.
 *
 * \param device Katherine device
 * \param config Configuration to apply
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, so its channel count is unknown.
 * \retval KATHERINE_E_INVAL if a source is outside the enumeration, or the
 *   control link rejected an argument of the send; see
 *   katherine_transport_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the readout did not acknowledge within the
 *   control link's receive timeout.
 * \retval KATHERINE_E_BAD_CRD if a response was not the length of a command
 *   response.
 * \retval KATHERINE_E_STRAY if the acknowledgement never arrived while
 *   responses to other commands kept coming.
 * \retval KATHERINE_E_IO if the send or receive failed at the OS level for a
 *   reason none of the other codes cover.
 * \retval KATHERINE_E_NOMEM if the send or receive ran out of memory.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be
 *   taken; see katherine_transport_last_os_error().
 */
katherine_error_t
katherine_set_tdc_config(katherine_device_t *device, const katherine_tdc_config_t *config)
{
    katherine_error_t res;

    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_enumerated;
    }

    // Both generations put one enable bit per channel in the low byte, and
    // differ in everything above it. The second carries a field per channel
    // holding the source with the edge in its top bit, and needs a further
    // bit set to run the unit at all. The first carries an edge mask in the
    // byte above the enables and has nowhere to put a source, so a channel
    // there timestamps whatever the firmware wired it to.
    //
    // The first generation's layout is transcribed from the reference
    // implementation rather than read off a firmware: neither
    // first-generation build available here handles this operation code.
    static const uint8_t gen2_field_bits    = 4;    // width of one channel's field
    static const uint8_t gen2_field_shift   = 8;    // where the fields start
    static const uint8_t gen2_global_enable = 5;    // bit that runs the unit
    static const uint8_t gen1_edge_shift    = 8;    // where the edge mask starts
    static const uint32_t gen2_falling_edge = 0x8u; // top bit of a field

    const bool gen2    = device->derived_info.gen >= 2;
    const uint8_t have = device->derived_info.tdc_channel_count;

    for (uint8_t i = 0; i < KATHERINE_MAX_TDC_CHANNEL_COUNT; ++i) {
        if (i >= have) break;

        if (config->channel[i].source > KATHERINE_TDC_SOURCE_CHIP_3_FEEDBACK) {
            res = KATHERINE_E_INVAL;
            goto err_check;
        }
    }

    uint32_t word = 0;
    bool any      = false;

    for (uint8_t i = 0; i < have; ++i) {
        if (!config->channel[i].enabled) continue;

        word |= UINT32_C(1) << i;
        any = true;
    }

    if (gen2) {
        if (any) word |= UINT32_C(1) << gen2_global_enable;

        for (uint8_t i = 0; i < have; ++i) {
            const uint32_t field = (uint32_t) config->channel[i].source
                | (config->channel[i].use_falling_edge ? gen2_falling_edge : 0u);
            word |= field << (gen2_field_shift + i * gen2_field_bits);
        }
    } else {
        // Bit 3 of the enables selects the chip's own feedback. Not exposed,
        // its semantics being unestablished.
        for (uint8_t i = 0; i < have; ++i) {
            if (config->channel[i].use_falling_edge) word |= UINT32_C(1) << (i + gen1_edge_shift);
        }
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_send6_i64(&device->control_transport, CMD_TYPE_INTERNAL_TDC_SETTINGS, (int64_t) word);
    if (res) goto err_send;

    res = katherine_cmd_wait_ack(&device->control_transport, CMD_TYPE_INTERNAL_TDC_SETTINGS);
    if (res) goto err_recv;

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_check:
err_enumerated:
    return res;
}

/**
 * Read how many edges the timing unit has timestamped on each channel.
 *
 * This function fills an array of size KATHERINE_MAX_TDC_CHANNEL_COUNT
 * with counts, one per channel. However, since the number of channels
 * differs depending on device model, the valid number of channels for the
 * current device is katherine_device_derived_info_t::tdc_channel_count.
 * The remaining counters will be filled with constant zeros.
 *
 * Counts not received are left unchanged, so a caller holding an error can
 * tell how far the sequence got by zeroing the array first.
 *
 * \param device Katherine device
 * \param counts Counts to fill (allocate KATHERINE_MAX_TDC_CHANNEL_COUNT)
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_STATE if the device has been neither enumerated nor
 *   declared, so its generation is unknown.
 * \retval KATHERINE_E_TIMEOUT if the readout stopped answering partway
 *   through the sequence, or did not start.
 * \retval KATHERINE_E_BAD_CRD if an answer was not a command response.
 * \retval KATHERINE_E_STRAY if another command's responses kept arriving
 *   until the discard budget ran out.
 * \retval KATHERINE_E_INVAL if the control link rejected an argument of a
 *   send; see katherine_transport_last_os_error().
 * \retval KATHERINE_E_IO if a send or receive failed at the OS level for a
 *   reason none of the other codes cover.
 * \retval KATHERINE_E_NOMEM if a send or receive ran out of memory.
 * \retval KATHERINE_E_SYSTEM if the control link's lock could not be
 *   taken; see katherine_transport_last_os_error().
 */
katherine_error_t
katherine_get_tdc_counts(katherine_device_t *device, uint32_t counts[KATHERINE_MAX_TDC_CHANNEL_COUNT])
{
    katherine_error_t res;

    if (!device->derived_info.supported) {
        res = KATHERINE_E_STATE;
        goto err_enumerated;
    }

    // Entries past this readout's own count answer for channels it does not
    // have, so they are zeroed rather than left holding whatever the caller's
    // array did.
    if (counts != NULL) {
        for (uint8_t i = device->derived_info.tdc_channel_count; i < KATHERINE_MAX_TDC_CHANNEL_COUNT; ++i) {
            counts[i] = 0;
        }
    }

    res = katherine_transport_mutex_lock(&device->control_transport);
    if (res) goto err_lock;

    katherine_transport_flush_incoming(&device->control_transport);

    res = katherine_cmd_send6(&device->control_transport, CMD_TYPE_INTERNAL_TDC_READ_COUNTS);
    if (res) goto err_send;

    // The sequence is read whole whatever the caller wants kept: answers left
    // on the link would be handed to whichever command came next.
    for (uint8_t i = 0; i < device->derived_info.tdc_channel_count; ++i) {
        char crd[KATHERINE_CMD_CRD_SIZE];
        res = katherine_cmd_wait_ack_crd(&device->control_transport, CMD_TYPE_INTERNAL_TDC_READ_COUNTS, crd);
        if (res) goto err_recv;

        if (counts == NULL) continue;

        const unsigned char *b = (const unsigned char *) crd;
        counts[i]              = (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
    }

    (void) katherine_transport_mutex_unlock(&device->control_transport);
    return KATHERINE_E_OK;

err_recv:
err_send:
    (void) katherine_transport_mutex_unlock(&device->control_transport);
err_lock:
err_enumerated:
    return res;
}
