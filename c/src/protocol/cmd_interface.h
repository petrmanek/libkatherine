/**
 * \file
 * \brief Internal command interface of the Katherine readout.
 * \author Petr Mánek
 * \date 9.6.18
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
#include <katherine/config.h>
#include <katherine/error.h>
#include <katherine/global.h>
#include <katherine/udp.h>
#include "bitfields.h"
#include "cmd_builder.h"

//
// IMPORTANT NOTICE:
//
// The following interface is internal.
// It is not intended for user application access.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

/**
 * Length of a command response datagram. The readout answers an
 * acknowledged command with exactly this many bytes, all zero but for the
 * operation-code byte, and fills in the leading bytes when the response
 * carries data. Nothing else ever reaches the command socket, so a datagram
 * of any other length is the peer violating the wire format.
 */
#define KATHERINE_CMD_CRD_SIZE           8

/**
 * Byte carrying the operation code of a command, and the response
 * identifier of a command response. Correlating the two is what pairs a
 * response with the request that provoked it.
 */
#define KATHERINE_CMD_OPCODE_BYTE        6

/**
 * Non-correlating response datagrams one katherine_cmd_wait_ack_crd() call
 * discards before it gives up with KATHERINE_E_STRAY.
 *
 * Every discard is a datagram that really arrived, so spending the budget
 * costs no receive timeout and a quiet peer never reaches it; the bound is
 * there so that a chatty or confused one cannot hold the caller forever.
 * Kept above the longest answer the readout is documented to send for a
 * single command -- the all-DAC scan replies twenty-two times -- so that the
 * leftovers of one such answer cannot exhaust the budget of the next
 * command even if nothing flushed them first.
 */
#define KATHERINE_CMD_MAX_STRAY_DISCARDS 32

/**
 * Stale datagrams katherine_cmd_drain() discards before it gives up.
 *
 * Generous because the drain never blocks: it stops at the first receive
 * that finds the socket empty, so the bound only caps the work a peer
 * flooding the socket can impose on one flush.
 */
#define KATHERINE_CMD_MAX_DRAIN          64

/**
 * Send a command, or any other exact-length buffer, to the readout.
 * \param udp Session to send on.
 * \param buffer Bytes to send.
 * \param count Number of bytes to send.
 *
 * \retval KATHERINE_E_OK on success, once every byte has been handed to the
 *   socket.
 * \retval KATHERINE_E_IO if the send failed at the OS level for a reason
 *   none of the other codes cover; see sendto(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_INVAL if the send reported an invalid argument; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the send ran out of memory; see sendto(2) and
 *   katherine_udp_last_os_error().
 */
static inline katherine_error_t
katherine_cmd_send(katherine_udp_t *udp, const void *buffer, size_t count)
{
    katherine_error_t res;

    res = katherine_udp_send_exact(udp, buffer, count);
    if (res) goto err;

    return KATHERINE_E_OK;

err:
    return res;
}

/**
 * Send a command that carries nothing but its opcode.
 *
 * Composing the datagram cannot fail, so every code below is the send's.
 *
 * \param udp Session to send on.
 * \param val6 Command opcode.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_IO if the send failed at the OS level for a reason
 *   none of the other codes cover; see sendto(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_INVAL if the send reported an invalid argument; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the send ran out of memory; see sendto(2) and
 *   katherine_udp_last_os_error().
 */
static inline katherine_error_t
katherine_cmd_send6(katherine_udp_t *udp, uint8_t val6)
{
    katherine_cmd_t cmd = katherine_cmd_create(val6);
    return katherine_cmd_send(udp, cmd.b, sizeof(cmd.b));
}

/**
 * Send a command whose only argument occupies the lowest payload byte, as
 * the hardware-command dispatch does with its sub-command number.
 *
 * Composing the datagram cannot fail, so every code below is the send's.
 *
 * \param udp Session to send on.
 * \param val6 Command opcode.
 * \param val0 Argument byte.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_IO if the send failed at the OS level for a reason
 *   none of the other codes cover; see sendto(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_INVAL if the send reported an invalid argument; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the send ran out of memory; see sendto(2) and
 *   katherine_udp_last_os_error().
 */
static inline katherine_error_t
katherine_cmd_send60(katherine_udp_t *udp, uint8_t val6, uint8_t val0)
{
    katherine_cmd_t cmd = katherine_cmd_create(val6);
    cmd.b[0]            = val0;
    return katherine_cmd_send(udp, cmd.b, sizeof(cmd.b));
}

/**
 * Send a command with both a sub-index and a payload word, as the DAC
 * setters and sensor-register writes use.
 *
 * Composing the datagram cannot fail -- a wider payload is truncated, never
 * rejected -- so every code below is the send's.
 *
 * \param udp Session to send on.
 * \param val6 Command opcode.
 * \param val4 Sub-index.
 * \param value Payload; only its low 32 bits reach the wire.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_IO if the send failed at the OS level for a reason
 *   none of the other codes cover; see sendto(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_INVAL if the send reported an invalid argument; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the send ran out of memory; see sendto(2) and
 *   katherine_udp_last_os_error().
 */
static inline katherine_error_t
katherine_cmd_send64_i64(katherine_udp_t *udp, uint8_t val6, uint8_t val4, int64_t value)
{
    katherine_cmd_t cmd = katherine_cmd_create(val6);
    katherine_cmd_payload_set_subidx(&cmd, val4);
    katherine_cmd_payload_set_i64(&cmd, value);
    return katherine_cmd_send(udp, cmd.b, sizeof(cmd.b));
}

/**
 * Send a command with a payload word and no sub-index.
 *
 * Composing the datagram cannot fail -- a wider payload is truncated, never
 * rejected -- so every code below is the send's.
 *
 * \param udp Session to send on.
 * \param val6 Command opcode.
 * \param value Payload; only its low 32 bits reach the wire.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_IO if the send failed at the OS level for a reason
 *   none of the other codes cover; see sendto(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_INVAL if the send reported an invalid argument; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the send ran out of memory; see sendto(2) and
 *   katherine_udp_last_os_error().
 */
static inline katherine_error_t
katherine_cmd_send6_i64(katherine_udp_t *udp, uint8_t val6, int64_t value)
{
    katherine_cmd_t cmd = katherine_cmd_create(val6);
    katherine_cmd_payload_set_i64(&cmd, value);
    return katherine_cmd_send(udp, cmd.b, sizeof(cmd.b));
}

/**
 * Send a command whose payload is a float rather than an integer.
 *
 * Composing the datagram cannot fail, so every code below is the send's.
 *
 * \param udp Session to send on.
 * \param val6 Command opcode.
 * \param value Payload, transmitted as its IEEE-754 bytes.
 *
 * \retval KATHERINE_E_OK on success.
 * \retval KATHERINE_E_IO if the send failed at the OS level for a reason
 *   none of the other codes cover; see sendto(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_INVAL if the send reported an invalid argument; see
 *   sendto(2) and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the send ran out of memory; see sendto(2) and
 *   katherine_udp_last_os_error().
 */
static inline katherine_error_t
katherine_cmd_send6_f32(katherine_udp_t *udp, uint8_t val6, float value)
{
    katherine_cmd_t cmd = katherine_cmd_create(val6);
    katherine_cmd_payload_set_f32(&cmd, value);
    return katherine_cmd_send(udp, cmd.b, sizeof(cmd.b));
}

//
// GeneralConfig sensor register (Timepix3 manual v2, sec 4.2.5.4.1, headers
// 0h30/0h31), declared as a bitfield for INSERT(). Field names follow the
// manual. Only the fields this library writes are declared: polarity,
// gray_count_en follows katherine_config_t, while ackcommand_en is pinned.
// Op_mode [2:1] and Fast_lo_en [6] belong to the acquisition-mode command,
// which merges them into the readout's register image after this word is
// written, so the values given here for them do not survive. Tp_en [5] and
// the test-pulse selectors stay 0: they travel in the dedicated test-pulse
// command instead.
#define _BITS_general_config_polarity_start      0
#define _BITS_general_config_polarity_mask       MASK(1)
#define _BITS_general_config_polarity_type       uint8_t

#define _BITS_general_config_gray_count_en_start 3
#define _BITS_general_config_gray_count_en_mask  MASK(1)
#define _BITS_general_config_gray_count_en_type  uint8_t

#define _BITS_general_config_ackcommand_en_start 4
#define _BITS_general_config_ackcommand_en_mask  MASK(1)
#define _BITS_general_config_ackcommand_en_type  uint8_t

#define _BITS_general_config_tp_en_start         5
#define _BITS_general_config_tp_en_mask          MASK(1)
#define _BITS_general_config_tp_en_type          uint8_t

#define _BITS_general_config_fast_lo_en_start    6
#define _BITS_general_config_fast_lo_en_mask     MASK(1)
#define _BITS_general_config_fast_lo_en_type     uint8_t

/**
 * Compose the GeneralConfig register word from a device configuration.
 *
 * Polarity 0 selects electron collection and 1 hole collection, and
 * gray_count_en gray-codes the pixel counters. ackcommand_en is pinned
 * because this library depends on command acknowledgements. Tp_en is left
 * clear: the test-pulse command carries the enable, and hardware A/B runs
 * confirm both analog and digital pulses fire regardless of this bit. fast_lo_en, the superpixel oscillator behind fast time
 * stamping, is set here only so the register starts in a defined state:
 * katherine_set_acq_mode() rewrites it, together with Op_mode, on every
 * acquisition.
 *
 * \param config Configuration to read the polarity, gray-coding and
 *   test-pulse settings from.
 * \return Register word, ready for a sensor-register write.
 */
static inline int32_t
katherine_general_config_word(const katherine_config_t *config)
{
    uint64_t word = 0;
    word          = INSERT(word, general_config, polarity, !config->polarity_holes);
    word          = INSERT(word, general_config, gray_count_en, !config->gray_disable);
    word          = INSERT(word, general_config, ackcommand_en, 1);
    word          = INSERT(word, general_config, fast_lo_en, 1);
    return (int32_t) word;
}

//
// PLLConfig sensor register (Timepix3 manual Table 16, sensor register 3),
// declared as a bitfield for INSERT(). The four one-bit fields at [3:0] are
// pinned: the PLL is switched on, held out of reset, sourced from its own
// Vcntrl DAC, and clocked on both edges. pll_out_config selects what leaves
// the PLL output pad.
#define _BITS_pll_config_bypass_pll_start             0
#define _BITS_pll_config_bypass_pll_mask              MASK(1)
#define _BITS_pll_config_bypass_pll_type              uint8_t

#define _BITS_pll_config_reset_pll_start              1
#define _BITS_pll_config_reset_pll_mask               MASK(1)
#define _BITS_pll_config_reset_pll_type               uint8_t

#define _BITS_pll_config_select_vcntrl_pll_dac_start  2
#define _BITS_pll_config_select_vcntrl_pll_dac_mask   MASK(1)
#define _BITS_pll_config_select_vcntrl_pll_dac_type   uint8_t

#define _BITS_pll_config_dual_edge_clock_start        3
#define _BITS_pll_config_dual_edge_clock_mask         MASK(1)
#define _BITS_pll_config_dual_edge_clock_type         uint8_t

#define _BITS_pll_config_clk_phaseshift_divider_start 4
#define _BITS_pll_config_clk_phaseshift_divider_mask  MASK(2)
#define _BITS_pll_config_clk_phaseshift_divider_type  uint8_t

#define _BITS_pll_config_clk_phaseshift_number_start  6
#define _BITS_pll_config_clk_phaseshift_number_mask   MASK(3)
#define _BITS_pll_config_clk_phaseshift_number_type   uint8_t

#define _BITS_pll_config_pll_out_config_start         9
#define _BITS_pll_config_pll_out_config_mask          MASK(5)
#define _BITS_pll_config_pll_out_config_type          uint8_t

/** pll_out_config value routing the shutter signal to the PLL output pad. */
#define KATHERINE_PLL_OUT_SHUTTER_OUT                 0x14

/**
 * Compose the PLLConfig register word from a device configuration.
 *
 * clk_phaseshift_divider is the clock frequency selector of Timepix3 manual
 * Table 17 and follows katherine_freq_t; clk_phaseshift_number is the phase
 * selector that same table clamps, and follows katherine_phase_t. Both are
 * masked to their field widths rather than validated, matching how the DAC
 * setters transmit unchecked values.
 *
 * \param config Configuration to read the phase and frequency from.
 * \return Register word, ready for a sensor-register write.
 */
static inline int32_t
katherine_pll_config_word(const katherine_config_t *config)
{
    uint64_t word = 0;
    word          = INSERT(word, pll_config, bypass_pll, 0);
    word          = INSERT(word, pll_config, reset_pll, 1);
    word          = INSERT(word, pll_config, select_vcntrl_pll_dac, 1);
    word          = INSERT(word, pll_config, dual_edge_clock, 1);
    word          = INSERT(word, pll_config, clk_phaseshift_divider, (uint8_t) config->freq);
    word          = INSERT(word, pll_config, clk_phaseshift_number, (uint8_t) config->phase);
    word          = INSERT(word, pll_config, pll_out_config, KATHERINE_PLL_OUT_SHUTTER_OUT);
    return (int32_t) word;
}


// ----------------------------------------------------------------------------
// From this point onward we define the instruction set supported by Katherine.

/**
 * Define katherine_cmd_<CMD_NAME>(udp), sending a command whose arguments
 * are all fixed at definition time.
 * \param A Sender to build on, without the katherine_ prefix (e.g. cmd6).
 * \param CMD_NAME Name of the generated function, without the
 *   katherine_cmd_ prefix.
 * \param ... Leading arguments of the sender, typically the opcode.
 */
#define K_DEFINE_CMD_ARG0(A, CMD_NAME, ...) \
    static inline katherine_error_t katherine_cmd_##CMD_NAME(katherine_udp_t *udp) \
    { \
        return katherine_##A(udp, __VA_ARGS__); \
    }

/**
 * Define katherine_cmd_<CMD_NAME>(udp, arg1), sending a command whose last
 * argument the caller supplies.
 * \param A Sender to build on, without the katherine_ prefix.
 * \param CMD_NAME Name of the generated function, without the
 *   katherine_cmd_ prefix.
 * \param ARG1_TYPE Type of the caller-supplied argument.
 * \param ... Leading arguments of the sender, which the caller's argument
 *   follows.
 */
#define K_DEFINE_CMD_ARG1(A, CMD_NAME, ARG1_TYPE, ...) \
    static inline katherine_error_t katherine_cmd_##CMD_NAME(katherine_udp_t *udp, ARG1_TYPE arg1) \
    { \
        return katherine_##A(udp, __VA_ARGS__, arg1); \
    }

/** Command opcodes of the readout protocol, carried in byte 6 of a command. */
typedef enum katherine_cmd_type {
    CMD_TYPE_ACQUISITION_TIME_SETTINGS_LSB = 0x01,
    CMD_TYPE_BIAS_SETTINGS                 = 0x02,
    CMD_TYPE_ACQUISITION_START             = 0x03,
    CMD_TYPE_INTERNAL_DAC_SETTINGS         = 0x04,
    CMD_TYPE_SEQ_READOUT_START             = 0x05,
    CMD_TYPE_ACQUISITION_STOP              = 0x06,
    CMD_TYPE_HW_COMMAND_START              = 0x07,
    CMD_TYPE_SENSOR_REGISTER_SETTING       = 0x08,
    CMD_TYPE_ACQUISITION_MODE_SETTING      = 0x09,
    CMD_TYPE_ACQUISITION_TIME_SETTING_MSB  = 0x0A,
    CMD_TYPE_ECHO_CHIP_ID                  = 0x0B,
    CMD_TYPE_GET_BIAS_VOLTAGE              = 0x0C,
    CMD_TYPE_GET_ADC_VOLTAGE               = 0x0D,
    CMD_TYPE_GET_BACK_READ_REGISTER        = 0x0E,

    /**
     * DAC-scan opcodes (this one and 0x14 below): the scan indexes chip DAC
     * codes 1-based, 1..18 for the named DACs plus 28..31 for BandGap /
     * BandGap_Temp / Ibias_dac / Ibias_dac_cas (Tpx3 manual Table 11) --
     * unlike CMD_TYPE_INTERNAL_DAC_SETTINGS above, which is 0-based 0..17.
     * An off-by-one trap for a future scan API that reuses katherine_dacs_t
     * indexing.
     */
    CMD_TYPE_INTERNAL_DAC_SCAN = 0x0F,

    CMD_TYPE_SET_PIXEL_CONFIG           = 0x10,
    CMD_TYPE_GET_PIXEL_CONFIG           = 0x11,
    CMD_TYPE_SET_ALL_PIXEL_CONFIG       = 0x12,
    CMD_TYPE_NUMBER_OF_FRAMES           = 0x13,
    CMD_TYPE_GET_ALL_DAC_SCAN           = 0x14, ///< Same 1-based DAC indexing as 0x0F above
    CMD_TYPE_GET_HW_READOUT_TEMPERATURE = 0x15,
    CMD_TYPE_LED_SETTINGS               = 0x16,
    CMD_TYPE_GET_READOUT_STATUS         = 0x17,
    CMD_TYPE_GET_COMMUNICATION_STATUS   = 0x18,
    CMD_TYPE_GET_SENSOR_TEMPERATURE     = 0x19,
    CMD_TYPE_DIGITAL_TEST               = 0x20,
    CMD_TYPE_ACQUISITION_SETUP          = 0x21,
    CMD_TYPE_GET_ACQUISITION_UNIT_DATA  = 0x22,
    CMD_TYPE_INTERNAL_TRIGGER_GENERATOR = 0x23,

    /**
     * The readout firmware answers this with response id 0x22
     * (GetAcquisitionUnitData), not 0x24 -- a firmware quirk, not a
     * transcription error, should a future caller key off the response id.
     */
    CMD_TYPE_TRIGGER_GENERATOR_SETUP_READ = 0x24,

    CMD_TYPE_TEST_PULSE_SETTING       = 0x26,
    CMD_TYPE_TOA_CALIBRATION_SETUP    = 0x28,
    CMD_TYPE_NUMBER_OF_TOKENS_SETTING = 0x29,
    // Generation-dependent: this opcode is absent from the Gen2 firmware's
    // mid-acquisition dispatcher, so it cannot be relied on during a
    // measurement there. 0x0D, the ADC channels, is present in that
    // dispatcher and reads readout-side registers only, which makes it the
    // candidate substitute for leakage monitoring mid-run.
    CMD_TYPE_GET_BIAS_CURRENT      = 0x30,
    CMD_TYPE_INTERNAL_TDC_SETTINGS = 0x32,

    /**
     * The readout firmware sends five counter datagrams for this command,
     * not six: a reader waiting for a sixth would hang.
     */
    CMD_TYPE_INTERNAL_TDC_READ_COUNTS = 0x33,

    CMD_TYPE_INTERFACE_SELECTION = 0x50,

    CMD_TYPE_USB_REDRIVER_SETTING = 0x98, ///< Gen2 hardware only

    CMD_TYPE_CHANGE_PORTS = 0xF0,
} katherine_cmd_type_t;

/**
 * Response identifier the readout answers an operation code under.
 *
 * Almost every command is answered under its own operation code, repeated in
 * byte 6 of the response. The two exceptions below are the readout
 * firmware's, not this library's, and they are the reason the correlation
 * has a tolerant mode at all: a client that insisted on the request's own
 * code would reject the answers of a real readout. Both are also recorded at
 * their enumerators above.
 *
 * \param opcode Operation code of the request.
 * \return Identifier its response carries.
 */
static inline uint8_t
katherine_cmd_reply_id(uint8_t opcode)
{
    switch (opcode) {
    // The trigger-generator read-back is answered under the acquisition-unit
    // read-back's identifier -- the firmware's own copy-paste, never
    // corrected.
    case CMD_TYPE_TRIGGER_GENERATOR_SETUP_READ: return (uint8_t) CMD_TYPE_GET_ACQUISITION_UNIT_DATA;

    // The all-DAC scan is answered twenty-two times, every datagram carrying
    // the single-DAC scan's identifier and never its own.
    case CMD_TYPE_GET_ALL_DAC_SCAN: return (uint8_t) CMD_TYPE_INTERNAL_DAC_SCAN;

    default: return opcode;
    }
}

/**
 * Whether the readout answers a command at all.
 *
 * The four below it acts on in silence, and a client that waited for an
 * acknowledgement of any of them would stall until its receive timeout
 * expired -- on the acquisition start and stop, once per acquisition. This
 * is the readout's behavior and not a matter of degree: there is no
 * acknowledgement to lose, so the wait must not be attempted. Every other
 * operation code of the table above is answered, an unrecognized one is
 * dropped without a reply (the firmware's dispatcher has no default branch),
 * and this function speaks only of the ones the table names.
 *
 * \param opcode Operation code of the request.
 * \return True if a response can be waited for.
 */
static inline bool
katherine_cmd_is_acknowledged(uint8_t opcode)
{
    switch (opcode) {
    // The acquisition start arms the readout; the measurement data stream is
    // the only answer it gives.
    case CMD_TYPE_ACQUISITION_START:

    // The readout-chain selection is applied silently.
    case CMD_TYPE_SEQ_READOUT_START:

    // The stop is observed through the end of the data stream instead; the
    // firmware's handler is an empty function.
    case CMD_TYPE_ACQUISITION_STOP:

    // Documented as a handshake, but the firmware's acknowledgement is
    // commented out. Not sent by this library today; listed so that a future
    // caller does not wait on it.
    case CMD_TYPE_INTERFACE_SELECTION: return false;

    default: return true;
    }
}

/**
 * Discard whatever a previous exchange left queued on a command session.
 *
 * The readout answers some commands more than once and the network may
 * deliver a response the client had already given up on, so a session can
 * hold datagrams belonging to no request in flight. Correlation recognizes
 * those and steps over them, but a leftover response of the *same* command
 * would correlate perfectly and be read as the answer to a later repetition
 * of it, which is how a session ends up one command out of step for good.
 * Flushing before a command closes that gap, and keeps the stray budget of
 * the wait that follows for genuine surprises.
 *
 * Never blocks: it stops at the first receive that finds nothing queued,
 * which is the ordinary case and must cost nothing, since every command of
 * the library pays for it. Best-effort by design and therefore returning
 * nothing -- a transport failure here resurfaces immediately at the send or
 * receive that follows, where the caller is already prepared for it. What
 * was discarded is counted in katherine_udp_t::stray_command_responses.
 *
 * \param udp Session to flush.
 */
static inline void
katherine_cmd_drain(katherine_udp_t *udp)
{
    // One byte of headroom, so that an oversized datagram is observed as
    // oversized rather than silently truncated to a plausible length.
    char crd[KATHERINE_CMD_CRD_SIZE + 1];

    for (uint32_t discarded = 0; discarded < KATHERINE_CMD_MAX_DRAIN; ++discarded) {
        size_t received = sizeof(crd);

        if (katherine_udp_recv_nowait(udp, crd, &received) != 0) return;

        ++udp->stray_command_responses;
    }
}

/**
 * Receive the command response belonging to one request, keeping its contents.
 *
 * Reads response datagrams until one correlates with the given operation
 * code, discarding and counting the rest. Correlation compares the
 * identifier in byte 6: the operation code itself always matches, and by
 * default so does the identifier the firmware substitutes for it
 * (katherine_cmd_reply_id()), unless the session is in strict mode
 * (katherine_udp_set_strict_ack()).
 *
 * A command the readout never answers is refused outright rather than waited
 * for; see katherine_cmd_is_acknowledged().
 *
 * \param udp Session to receive on.
 * \param opcode Operation code of the request whose response this is.
 * \param crd Storage for the KATHERINE_CMD_CRD_SIZE response bytes, or NULL
 *   to discard them.
 *
 * \retval KATHERINE_E_OK on success, the correlating response then copied to
 *   crd unless that is NULL.
 * \retval KATHERINE_E_TIMEOUT if no datagram arrived before the session's
 *   receive timeout expired, or a pinned session spent its
 *   KATHERINE_UDP_PIN_MAX_DISCARDS budget on datagrams from other hosts.
 *   Neither route records an OS error, and the
 *   retrying callers of the slow commands depend on seeing this code
 *   unchanged.
 * \retval KATHERINE_E_BAD_CRD if a datagram arrived whose length was not
 *   KATHERINE_CMD_CRD_SIZE, which is a length no command response has.
 *   Reported at the first such datagram, whatever identifier it carries, and
 *   without spending the stray budget.
 * \retval KATHERINE_E_STRAY if KATHERINE_CMD_MAX_STRAY_DISCARDS correctly
 *   sized responses arrived whose identifier byte matched neither the
 *   operation code nor the substitute -- responses belonging to some other
 *   request -- and the awaited one never did. Each of them is counted in
 *   katherine_udp_t::stray_command_responses.
 * \retval KATHERINE_E_INVAL if the readout does not answer this operation
 *   code at all, refused before any datagram is waited for, or if the
 *   receive reported an invalid argument; see recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_IO if the receive failed at the OS level for a reason
 *   none of the other codes cover; see recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the receive ran out of memory; see
 *   recvfrom(2) and katherine_udp_last_os_error().
 */
static inline katherine_error_t
katherine_cmd_wait_ack_crd(katherine_udp_t *udp, uint8_t opcode, char *crd)
{
    // The second identifier this wait accepts. In strict mode there is no
    // second one, so it collapses onto the request's own operation code.
    const uint8_t substitute = udp->strict_ack ? opcode : katherine_cmd_reply_id(opcode);

    // One byte of headroom, so that an oversized datagram is observed as
    // oversized rather than silently truncated to a plausible length.
    char buf[KATHERINE_CMD_CRD_SIZE + 1];

    if (!katherine_cmd_is_acknowledged(opcode)) return KATHERINE_E_INVAL;

    for (uint32_t discarded = 0; discarded < KATHERINE_CMD_MAX_STRAY_DISCARDS; ++discarded) {
        size_t received = sizeof(buf);
        uint8_t reply_id;

        katherine_error_t res = katherine_udp_recv(udp, buf, &received);
        if (res) return res;

        // Consumed either way, so reporting this does not leave the session
        // wedged behind the offending datagram.
        if (received != KATHERINE_CMD_CRD_SIZE) return KATHERINE_E_BAD_CRD;

        reply_id = (uint8_t) buf[KATHERINE_CMD_OPCODE_BYTE];
        if (reply_id == opcode || reply_id == substitute) {
            if (crd != NULL) memcpy(crd, buf, KATHERINE_CMD_CRD_SIZE);
            return KATHERINE_E_OK;
        }

        ++udp->stray_command_responses;
    }

    return KATHERINE_E_STRAY;
}

/**
 * Receive the command response belonging to one request and discard it, for
 * the commands whose acknowledgement carries no payload of interest.
 * \param udp Session to receive on.
 * \param opcode Operation code of the request whose response this is.
 *
 * \retval KATHERINE_E_OK on success: a response correlating with opcode
 *   arrived and was discarded.
 * \retval KATHERINE_E_TIMEOUT if no response arrived within the session's
 *   receive timeout, or if a pinned session spent its discard budget on
 *   datagrams from other hosts.
 * \retval KATHERINE_E_BAD_CRD if a datagram arrived that was not the size a
 *   command response has.
 * \retval KATHERINE_E_STRAY if well-formed responses kept arriving that
 *   belonged to other requests, until the discard budget ran out.
 * \retval KATHERINE_E_INVAL if the readout never answers this operation
 *   code, so waiting for it could only time out, or if the receive itself
 *   rejected its arguments; see recvfrom(2).
 * \retval KATHERINE_E_IO if the response could not be received for a reason
 *   none of the other codes cover; see recvfrom(2) and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the kernel could not allocate for the
 *   receive; see recvfrom(2).
 *
 * \see katherine_cmd_wait_ack_crd(), which this forwards to and which
 *   describes what separates a malformed response from a stray one.
 */
static inline katherine_error_t
katherine_cmd_wait_ack(katherine_udp_t *udp, uint8_t opcode)
{
    return katherine_cmd_wait_ack_crd(udp, opcode, NULL);
}

/**
 * Exchange one command with the readout: flush, send, and receive the
 * response that belongs to it.
 *
 * This is the whole of an acknowledged command whose request is a single
 * datagram. The stateful uploads, whose data travels between the command and
 * its acknowledgement, compose the three steps themselves instead.
 *
 * The operation code is read out of the buffer rather than passed
 * separately, so the response cannot be correlated against a code the
 * request did not carry.
 *
 * \param udp Session to exchange on.
 * \param buffer Command datagram to send.
 * \param count Its length; at least KATHERINE_CMD_CRD_SIZE, since a shorter
 *   datagram carries no operation code.
 * \param crd Storage for the KATHERINE_CMD_CRD_SIZE response bytes, or NULL
 *   to discard them.
 *
 * \retval KATHERINE_E_OK on success, the response then copied to crd unless
 *   that is NULL.
 * \retval KATHERINE_E_INVAL for a command too short to carry an operation
 *   code, or one the readout never answers -- both reported before anything
 *   is sent, so the readout is left untouched -- or if the send or the
 *   receive reported an invalid argument; see sendto(2), recvfrom(2), and
 *   katherine_udp_last_os_error().
 * \retval KATHERINE_E_TIMEOUT if the readout did not answer within the
 *   session's receive timeout; see katherine_cmd_wait_ack_crd().
 * \retval KATHERINE_E_BAD_CRD if the response datagram was not
 *   KATHERINE_CMD_CRD_SIZE bytes long; see katherine_cmd_wait_ack_crd().
 * \retval KATHERINE_E_STRAY if responses to other requests kept arriving
 *   until the stray budget ran out, this command's own never among them; see
 *   katherine_cmd_wait_ack_crd(). The flush above makes this the peer's
 *   doing rather than a leftover of the previous exchange.
 * \retval KATHERINE_E_IO if the send or the receive failed at the OS level
 *   for a reason none of the other codes cover; see sendto(2), recvfrom(2),
 *   and katherine_udp_last_os_error().
 * \retval KATHERINE_E_NOMEM if the send or the receive ran out of memory;
 *   see sendto(2), recvfrom(2), and katherine_udp_last_os_error().
 */
static inline katherine_error_t
katherine_cmd_transact(katherine_udp_t *udp, const void *buffer, size_t count, char *crd)
{
    const uint8_t *bytes = (const uint8_t *) buffer;
    uint8_t opcode;
    katherine_error_t res;

    if (count < KATHERINE_CMD_CRD_SIZE) return KATHERINE_E_INVAL;

    opcode = bytes[KATHERINE_CMD_OPCODE_BYTE];
    if (!katherine_cmd_is_acknowledged(opcode)) return KATHERINE_E_INVAL;

    katherine_cmd_drain(udp);

    res = katherine_cmd_send(udp, buffer, count);
    if (res) return res;

    return katherine_cmd_wait_ack_crd(udp, opcode, crd);
}

// clang-format off
K_DEFINE_CMD_ARG0(cmd_send6,      set_all_pixel_config,                     CMD_TYPE_SET_ALL_PIXEL_CONFIG)
K_DEFINE_CMD_ARG0(cmd_send6,      echo_chip_id,                             CMD_TYPE_ECHO_CHIP_ID)
K_DEFINE_CMD_ARG0(cmd_send6,      get_readout_temperature,                  CMD_TYPE_GET_HW_READOUT_TEMPERATURE)
K_DEFINE_CMD_ARG0(cmd_send6,      get_sensor_temperature,                   CMD_TYPE_GET_SENSOR_TEMPERATURE)
K_DEFINE_CMD_ARG0(cmd_send6,      get_readout_status,                       CMD_TYPE_GET_READOUT_STATUS)
K_DEFINE_CMD_ARG0(cmd_send6,      get_comm_status,                          CMD_TYPE_GET_COMMUNICATION_STATUS)
K_DEFINE_CMD_ARG0(cmd_send6,      digital_test,                             CMD_TYPE_DIGITAL_TEST)
// clang-format on

/**
 * Sub-commands of CMD_TYPE_HW_COMMAND_START, dispatched by the readout to
 * the sensor's own command interface.
 */
typedef enum katherine_hw_cmd_type {
    CMD_START_SENSOR_CONFIG_REGISTERS_UPDATE        = 0,
    CMD_START_INTERNAL_DAC_UPDATE                   = 1,
    CMD_START_INTERNAL_DAC_BACK_READ                = 2,
    CMD_START_TIMER_READ                            = 3,
    CMD_START_TIMER_SET                             = 4,
    CMD_START_RESET_MATRIX_SEQUENTIAL               = 5,
    CMD_START_STOP_MATRIX_COMMAND                   = 6,
    CMD_START_LOAD_COLUMN_TEST_PULSE_REGISTER       = 7,
    CMD_START_READ_COLUMN_TEST_PULSE_REGISTER       = 8,
    CMD_START_LOAD_PIXEL_REGISTER_CONFIGURATION     = 9,
    CMD_START_READ_PIXEL_REGISTER_CONFIGURATION     = 10,
    CMD_START_READ_PIXEL_MATRIX_SEQUENTIAL          = 11,
    CMD_START_READ_PIXEL_MATRIX_DATA_DRIVEN_SETTING = 12,
    CMD_START_CHIP_ID_READ                          = 13,
    CMD_START_OUTPUT_BLOCK_CONFIG_UPDATE            = 14,
    CMD_START_DIGITAL_TEST                          = 15,
} katherine_hw_cmd_type_t;

// clang-format off
K_DEFINE_CMD_ARG0(cmd_send60,     hw_sensor_config_registers_update,        CMD_TYPE_HW_COMMAND_START, CMD_START_SENSOR_CONFIG_REGISTERS_UPDATE)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_internal_dac_update,                   CMD_TYPE_HW_COMMAND_START, CMD_START_INTERNAL_DAC_UPDATE)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_internal_dac_back_read,                CMD_TYPE_HW_COMMAND_START, CMD_START_INTERNAL_DAC_BACK_READ)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_timer_read,                            CMD_TYPE_HW_COMMAND_START, CMD_START_TIMER_READ)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_timer_set,                             CMD_TYPE_HW_COMMAND_START, CMD_START_TIMER_SET)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_reset_matrix_sequential,               CMD_TYPE_HW_COMMAND_START, CMD_START_RESET_MATRIX_SEQUENTIAL)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_stop_matrix_command,                   CMD_TYPE_HW_COMMAND_START, CMD_START_STOP_MATRIX_COMMAND)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_load_column_test_pulse_register,       CMD_TYPE_HW_COMMAND_START, CMD_START_LOAD_COLUMN_TEST_PULSE_REGISTER)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_read_column_test_pulse_register,       CMD_TYPE_HW_COMMAND_START, CMD_START_READ_COLUMN_TEST_PULSE_REGISTER)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_load_pixel_register_configuration,     CMD_TYPE_HW_COMMAND_START, CMD_START_LOAD_PIXEL_REGISTER_CONFIGURATION)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_read_pixel_register_configuration,     CMD_TYPE_HW_COMMAND_START, CMD_START_READ_PIXEL_REGISTER_CONFIGURATION)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_read_pixel_matrix_sequential_setting,  CMD_TYPE_HW_COMMAND_START, CMD_START_READ_PIXEL_MATRIX_SEQUENTIAL)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_read_pixel_matrix_data_driven_setting, CMD_TYPE_HW_COMMAND_START, CMD_START_READ_PIXEL_MATRIX_DATA_DRIVEN_SETTING)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_chip_id_read,                          CMD_TYPE_HW_COMMAND_START, CMD_START_CHIP_ID_READ)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_output_block_config_update,            CMD_TYPE_HW_COMMAND_START, CMD_START_OUTPUT_BLOCK_CONFIG_UPDATE)
K_DEFINE_CMD_ARG0(cmd_send60,     hw_digital_test,                          CMD_TYPE_HW_COMMAND_START, CMD_START_DIGITAL_TEST)

K_DEFINE_CMD_ARG1(cmd_send6_i64,  set_acqtime_lsb,                          int64_t, CMD_TYPE_ACQUISITION_TIME_SETTINGS_LSB)
K_DEFINE_CMD_ARG1(cmd_send6_i64,  set_acqtime_msb,                          int64_t, CMD_TYPE_ACQUISITION_TIME_SETTING_MSB)
K_DEFINE_CMD_ARG1(cmd_send6_i64,  set_number_of_frames,                     int64_t, CMD_TYPE_NUMBER_OF_FRAMES)
K_DEFINE_CMD_ARG1(cmd_send6_i64,  set_seq_readout_start,                    int64_t, CMD_TYPE_SEQ_READOUT_START)
K_DEFINE_CMD_ARG1(cmd_send6_i64,  start_acquisition,                        uint8_t, CMD_TYPE_ACQUISITION_START)
K_DEFINE_CMD_ARG1(cmd_send6_i64,  stop_acquisition,                         uint8_t, CMD_TYPE_ACQUISITION_STOP)
K_DEFINE_CMD_ARG1(cmd_send6_i64,  get_adc_voltage,                          uint8_t, CMD_TYPE_GET_ADC_VOLTAGE)
K_DEFINE_CMD_ARG1(cmd_send6_f32,  set_bias_settings,                        float, CMD_TYPE_BIAS_SETTINGS)

K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_ibias_preamp_on,                  int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 0)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_ibias_preamp_off,                 int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 1)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_vpreamp_ncas,                     int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 2)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_ibias_ikrum,                      int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 3)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_vfbk,                             int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 4)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_vthreshold_fine,                  int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 5)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_vthreshold_coarse,                int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 6)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_ibias_discs1_on,                  int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 7)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_ibias_discs1_off,                 int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 8)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_ibias_discs2_on,                  int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 9)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_ibias_discs2_off,                 int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 10)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_ibias_pixeldac,                   int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 11)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_ibias_tpbufferin,                 int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 12)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_ibias_tpbufferout,                int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 13)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_vtp_coarse,                       int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 14)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_vtp_fine,                         int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 15)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_ibias_cp_pll,                     int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 16)
K_DEFINE_CMD_ARG1(cmd_send64_i64, set_dac_pll_vcntrl,                       int64_t, CMD_TYPE_INTERNAL_DAC_SETTINGS, 17)
// clang-format on

#undef K_DEFINE_CMD_ARG0
#undef K_DEFINE_CMD_ARG1

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
