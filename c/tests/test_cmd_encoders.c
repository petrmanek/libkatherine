/**
 * @file
 * @brief Golden byte-vector tests for the command encoders in command_interface.h.
 *
 * Wire format: every command is one 8-byte little-endian UDP datagram --
 * byte[6] carries the opcode, byte[4] a sub-index where the command has one,
 * bytes[0..3] the payload; byte[5] is the chip index and byte[7] the high
 * opcode byte, both always zero for this single-chip device generation. Two
 * katherine_udp_t endpoints sit on loopback, one pinned to capture what the
 * other sends; each case invokes an encoder directly and captures the
 * resulting datagram for a byte-exact comparison against a hand-written
 * expected array.
 *
 * Covers, in order:
 *   1. The five encoding primitives (katherine_cmd6, katherine_cmd60,
 *      katherine_cmd6_i64, katherine_cmd64_i64, katherine_cmd6_float), plus
 *      a couple of boundary vectors that freeze correct edge-case behavior
 *      of the payload store in katherine_cmd_i64.
 *   2. Every ARG0 wrapper (base commands and the 16 hw_* sub-commands).
 *   3. Every ARG1 wrapper (the plain settings, set_bias_settings, and the
 *      18 set_dac_* wrappers).
 *   4. Boundary vectors for katherine_cmd_i64: values whose low 32 bits are
 *      all that ever reach the wire, and a negative value, which encodes
 *      via the two's complement bit pattern of those low 32 bits.
 *   5. katherine_general_config_word() (config.c, via command_interface.h):
 *      the GeneralConfig sensor register value built from named bits of
 *      katherine_config_t, encoded the same way katherine_configure()
 *      sends it.
 *
 * The 0x26 test-pulse datagram already has byte-exact coverage in
 * test_tp.c and is not repeated here.
 *
 * @author Petr Mánek
 * @date 21.8.26
 *
 * @copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <katherine/config.h>
#include <katherine/device.h>
#include <katherine/error.h>
#include <katherine/udp.h>

#include "protocol/command_interface.h"
#include "ktest.h"

/* ------------------------------------------------------------------ */
/* Loopback fixture: one capture endpoint pinned to a sender pointed at */
/* it. Shared by every test below -- these primitives never wait for   */
/* an acknowledgement, so a single send/recv pair per case is enough.  */

/* High, uncommon ports of their own, distinct from every other fixed pair
   registered in this tree (test_udp_pinning.c's 42555-42557, bench_udp.c's
   47301/47302/47311/47312, the ksim daemon's 1555/1556): this fixture claims
   no global resource, so the test needs no exclusive slot among the others. */
#define PORT_SENDER        42600
#define PORT_CAPTURE       42601

/* Receive timeout of the capture endpoint. It serves two purposes: bounding
   the wait for the datagram a case just sent (always already queued by the
   time capture() runs, loopback delivery being complete before
   katherine_udp_send_exact() returns, so this is pure headroom against
   scheduling jitter) and, on the drain check below, bounding how long an
   empty read actually blocks -- so it is kept short rather than generous,
   since the drain check pays this cost once per CHECK_CMD case. */
#define CAPTURE_TIMEOUT_MS 20

static katherine_udp_t g_capture;
static katherine_udp_t g_sender;

static int
fixture_init(void)
{
    int res = katherine_udp_init_bound(
        &g_capture, "127.0.0.1", PORT_CAPTURE, "127.0.0.1", PORT_SENDER, CAPTURE_TIMEOUT_MS);
    if (res != 0) return res;

    /* Pin the capture endpoint to the sender's address, so a stray datagram
       from any other socket on the box can never be mistaken for part of
       this test's traffic. */
    katherine_udp_pin_remote(&g_capture);

    res = katherine_udp_init_bound(&g_sender, "127.0.0.1", PORT_SENDER, "127.0.0.1", PORT_CAPTURE, CAPTURE_TIMEOUT_MS);
    if (res != 0) {
        katherine_udp_fini(&g_capture);
        return res;
    }

    return 0;
}

static void
fixture_fini(void)
{
    katherine_udp_fini(&g_sender);
    katherine_udp_fini(&g_capture);
}

/* True if res is the code an expired receive timeout yields, which is also
   what a pinned receive reports once its discard budget is spent (see
   katherine_udp_pin_remote()). */
static bool
is_timeout(int res)
{
    return res == -KATHERINE_E_TIMEOUT;
}

/* Captures the next datagram sent by g_sender into got[8]. The recv buffer
   is oversized to 16 bytes and the received length is required to be
   exactly 8, so an encoder that emits a short or long datagram fails here
   instead of being silently truncated or padded into a false match. Once
   the datagram is read, the endpoint must be empty: a second recv is
   required to time out (a short-timeout recv reporting -KATHERINE_E_TIMEOUT
   proving emptiness, in place of the MSG_DONTWAIT drain-check a raw socket
   would use), so a future two-datagram encoder is caught at its own
   CHECK_CMD instead of desynchronizing every case that runs after it. */
static void
capture(unsigned char got[8])
{
    unsigned char buf[16];
    memset(buf, 0xAA, sizeof(buf));
    size_t n = sizeof(buf);
    (void) katherine_udp_recv(&g_capture, buf, &n);
    KT_CHECK_EQ(n, 8);
    memcpy(got, buf, 8);

    unsigned char drain[16];
    size_t drain_n = sizeof(drain);
    KT_CHECK(is_timeout(katherine_udp_recv(&g_capture, drain, &drain_n)));
}

/* Sends one command via `send_expr`, captures the reply datagram, and
   compares it byte-for-byte against the explicit b0..b7 expectation. Each
   invocation keeps its own source line, so a failing case is reported at
   the line of the specific command it checks, not at a shared helper. */
#define CHECK_CMD(send_expr, b0, b1, b2, b3, b4, b5, b6, b7) \
    do { \
        unsigned char kt_got_[8]; \
        const unsigned char kt_expected_[8] = {(unsigned char) (b0), (unsigned char) (b1), (unsigned char) (b2), \
            (unsigned char) (b3), (unsigned char) (b4), (unsigned char) (b5), (unsigned char) (b6), \
            (unsigned char) (b7)}; \
        KT_CHECK((send_expr) == 0); \
        capture(kt_got_); \
        KT_CHECK_MEM_EQ(kt_got_, kt_expected_, 8); \
    } while (0)

/* ------------------------------------------------------------------ */
/* 1. The five encoding primitives                                     */

static void
test_primitive_cmd6(void)
{
    CHECK_CMD(katherine_cmd6(&g_sender, (char) 0x5A), 0, 0, 0, 0, 0, 0, 0x5A, 0);
}

static void
test_primitive_cmd60(void)
{
    CHECK_CMD(katherine_cmd60(&g_sender, (char) 0x11, (char) 0x22), 0x22, 0, 0, 0, 0, 0, 0x11, 0);
}

static void
test_primitive_cmd6_i64(void)
{
    /* value = 0x0A0B0C0D -> little-endian payload 0D 0C 0B 0A. */
    CHECK_CMD(katherine_cmd6_i64(&g_sender, (char) 0x2A, (int64_t) 0x0A0B0C0D), 0x0D, 0x0C, 0x0B, 0x0A, 0, 0, 0x2A, 0);

    /* value = 0xFFFFFFFF: the payload store only ever writes cmd[0..3],
       so the top of the representable 32-bit range still leaves byte[4]
       at its zero-initialized value. This pins down the same boundary
       that test_i64_boundary() exercises from the other side, with values
       whose bits extend past bit 31. */
    CHECK_CMD(
        katherine_cmd6_i64(&g_sender, (char) 0x2A, (int64_t) 0xFFFFFFFF), 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0x2A, 0);
}

static void
test_primitive_cmd64_i64(void)
{
    /* value = 0x0E0F1011 -> little-endian payload 11 10 0F 0E; sub-index 0x09. */
    CHECK_CMD(katherine_cmd64_i64(&g_sender, (char) 0x04, (char) 0x09, (int64_t) 0x0E0F1011), 0x11, 0x10, 0x0F, 0x0E,
        0x09, 0, 0x04, 0);

    /* value = 0: the payload store only ever writes cmd[0..3], so cmd[4]
       keeps the sub-index that katherine_cmd64_i64 had already written
       before calling into it. This is correct behavior worth freezing on
       its own: it is exactly what katherine_set_dacs() (config.c) relies
       on when a caller configures a DAC to value 0 -- the sub-index must
       survive so the datagram still addresses the right DAC instead of
       silently reading as DAC 0. */
    CHECK_CMD(katherine_cmd64_i64(&g_sender, (char) 0x04, (char) 0x04, (int64_t) 0), 0, 0, 0, 0, 0x04, 0, 0x04, 0);
}

static void
test_primitive_cmd6_float(void)
{
    /* 5.0f -> IEEE-754 0x40A00000, little-endian bytes 00 00 A0 40. */
    CHECK_CMD(katherine_cmd6_float(&g_sender, (char) 0x33, 5.0f), 0x00, 0x00, 0xA0, 0x40, 0, 0, 0x33, 0);

    /* -1.5f -> IEEE-754 0xBFC00000, little-endian bytes 00 00 C0 BF. */
    CHECK_CMD(katherine_cmd6_float(&g_sender, (char) 0x44, -1.5f), 0x00, 0x00, 0xC0, 0xBF, 0, 0, 0x44, 0);
}

/* ------------------------------------------------------------------ */
/* 2a. ARG0 wrappers: base commands (katherine_cmd6, opcode at byte[6]) */

static void
test_wrappers_arg0_base(void)
{
    CHECK_CMD(katherine_cmd_set_all_pixel_config(&g_sender), 0, 0, 0, 0, 0, 0, 0x12, 0);
    CHECK_CMD(katherine_cmd_echo_chip_id(&g_sender), 0, 0, 0, 0, 0, 0, 0x0B, 0);
    CHECK_CMD(katherine_cmd_get_readout_temperature(&g_sender), 0, 0, 0, 0, 0, 0, 0x15, 0);
    CHECK_CMD(katherine_cmd_get_sensor_temperature(&g_sender), 0, 0, 0, 0, 0, 0, 0x19, 0);
    CHECK_CMD(katherine_cmd_get_readout_status(&g_sender), 0, 0, 0, 0, 0, 0, 0x17, 0);
    CHECK_CMD(katherine_cmd_get_comm_status(&g_sender), 0, 0, 0, 0, 0, 0, 0x18, 0);
    CHECK_CMD(katherine_cmd_digital_test(&g_sender), 0, 0, 0, 0, 0, 0, 0x20, 0);
}

/* ------------------------------------------------------------------ */
/* 2b. ARG0 wrappers: hw_* sub-commands (katherine_cmd60, opcode 0x07,  */
/* sub-command at byte[0])                                             */

static void
test_wrappers_hw(void)
{
    CHECK_CMD(katherine_cmd_hw_sensor_config_registers_update(&g_sender), 0, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_internal_dac_update(&g_sender), 1, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_internal_dac_back_read(&g_sender), 2, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_timer_read(&g_sender), 3, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_timer_set(&g_sender), 4, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_reset_matrix_sequential(&g_sender), 5, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_stop_matrix_command(&g_sender), 6, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_load_column_test_pulse_register(&g_sender), 7, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_read_column_test_pulse_register(&g_sender), 8, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_load_pixel_register_configuration(&g_sender), 9, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_read_pixel_register_configuration(&g_sender), 0x0A, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_read_pixel_matrix_sequential_setting(&g_sender), 0x0B, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_read_pixel_matrix_data_driven_setting(&g_sender), 0x0C, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_chip_id_read(&g_sender), 0x0D, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_output_block_config_update(&g_sender), 0x0E, 0, 0, 0, 0, 0, 0x07, 0);
    CHECK_CMD(katherine_cmd_hw_digital_test(&g_sender), 0x0F, 0, 0, 0, 0, 0, 0x07, 0);
}

/* ------------------------------------------------------------------ */
/* 3a. ARG1 wrappers built on katherine_cmd6_i64 (payload at [0..3],   */
/* opcode at [6])                                                      */

static void
test_wrappers_arg1_i64(void)
{
    /* 0x01020304 -> LE 04 03 02 01. */
    CHECK_CMD(katherine_cmd_set_acqtime_lsb(&g_sender, (int64_t) 0x01020304), 0x04, 0x03, 0x02, 0x01, 0, 0, 0x01, 0);
    /* 0x05060708 -> LE 08 07 06 05. */
    CHECK_CMD(katherine_cmd_set_acqtime_msb(&g_sender, (int64_t) 0x05060708), 0x08, 0x07, 0x06, 0x05, 0, 0, 0x0A, 0);
    /* 0x090A0B0C -> LE 0C 0B 0A 09. */
    CHECK_CMD(
        katherine_cmd_set_number_of_frames(&g_sender, (int64_t) 0x090A0B0C), 0x0C, 0x0B, 0x0A, 0x09, 0, 0, 0x13, 0);
    /* 0x11121314 -> LE 14 13 12 11. */
    CHECK_CMD(
        katherine_cmd_set_seq_readout_start(&g_sender, (int64_t) 0x11121314), 0x14, 0x13, 0x12, 0x11, 0, 0, 0x05, 0);
    CHECK_CMD(katherine_cmd_start_acquisition(&g_sender, (uint8_t) 0x2A), 0x2A, 0, 0, 0, 0, 0, 0x03, 0);
    CHECK_CMD(katherine_cmd_stop_acquisition(&g_sender, (uint8_t) 0x17), 0x17, 0, 0, 0, 0, 0, 0x06, 0);
    CHECK_CMD(katherine_cmd_get_adc_voltage(&g_sender, (uint8_t) 0x08), 0x08, 0, 0, 0, 0, 0, 0x0D, 0);
}

/* ------------------------------------------------------------------ */
/* 3b. ARG1 wrapper built on katherine_cmd6_float                      */

static void
test_wrappers_set_bias_settings(void)
{
    /* 42.0f -> IEEE-754 0x42280000, LE bytes 00 00 28 42. */
    CHECK_CMD(katherine_cmd_set_bias_settings(&g_sender, 42.0f), 0x00, 0x00, 0x28, 0x42, 0, 0, 0x02, 0);
}

/* ------------------------------------------------------------------ */
/* 3c. ARG1 wrappers built on katherine_cmd64_i64: 18 DAC setters      */
/* (opcode 0x04, sub-index at byte[4] in declaration order 0..17).     */
/* Each uses value = 0x1050 + <sub-index>, so the payload's low byte   */
/* (0x50..0x61) never collides with the sub-index (0x00..0x11) and the */
/* constant high byte 0x10 pins down the little-endian byte order.     */

static void
test_wrappers_dac(void)
{
    CHECK_CMD(
        katherine_cmd_set_dac_ibias_preamp_on(&g_sender, (int64_t) 0x1050), 0x50, 0x10, 0, 0, 0x00, 0, 0x04, 0);
    CHECK_CMD(
        katherine_cmd_set_dac_ibias_preamp_off(&g_sender, (int64_t) 0x1051), 0x51, 0x10, 0, 0, 0x01, 0, 0x04, 0);
    CHECK_CMD(katherine_cmd_set_dac_vpreamp_ncas(&g_sender, (int64_t) 0x1052), 0x52, 0x10, 0, 0, 0x02, 0, 0x04, 0);
    CHECK_CMD(katherine_cmd_set_dac_ibias_ikrum(&g_sender, (int64_t) 0x1053), 0x53, 0x10, 0, 0, 0x03, 0, 0x04, 0);
    CHECK_CMD(katherine_cmd_set_dac_vfbk(&g_sender, (int64_t) 0x1054), 0x54, 0x10, 0, 0, 0x04, 0, 0x04, 0);
    CHECK_CMD(
        katherine_cmd_set_dac_vthreshold_fine(&g_sender, (int64_t) 0x1055), 0x55, 0x10, 0, 0, 0x05, 0, 0x04, 0);
    CHECK_CMD(
        katherine_cmd_set_dac_vthreshold_coarse(&g_sender, (int64_t) 0x1056), 0x56, 0x10, 0, 0, 0x06, 0, 0x04, 0);
    CHECK_CMD(
        katherine_cmd_set_dac_ibias_discs1_on(&g_sender, (int64_t) 0x1057), 0x57, 0x10, 0, 0, 0x07, 0, 0x04, 0);
    CHECK_CMD(
        katherine_cmd_set_dac_ibias_discs1_off(&g_sender, (int64_t) 0x1058), 0x58, 0x10, 0, 0, 0x08, 0, 0x04, 0);
    CHECK_CMD(
        katherine_cmd_set_dac_ibias_discs2_on(&g_sender, (int64_t) 0x1059), 0x59, 0x10, 0, 0, 0x09, 0, 0x04, 0);
    CHECK_CMD(
        katherine_cmd_set_dac_ibias_discs2_off(&g_sender, (int64_t) 0x105A), 0x5A, 0x10, 0, 0, 0x0A, 0, 0x04, 0);
    CHECK_CMD(
        katherine_cmd_set_dac_ibias_pixeldac(&g_sender, (int64_t) 0x105B), 0x5B, 0x10, 0, 0, 0x0B, 0, 0x04, 0);
    CHECK_CMD(
        katherine_cmd_set_dac_ibias_tpbufferin(&g_sender, (int64_t) 0x105C), 0x5C, 0x10, 0, 0, 0x0C, 0, 0x04, 0);
    CHECK_CMD(
        katherine_cmd_set_dac_ibias_tpbufferout(&g_sender, (int64_t) 0x105D), 0x5D, 0x10, 0, 0, 0x0D, 0, 0x04, 0);
    CHECK_CMD(katherine_cmd_set_dac_vtp_coarse(&g_sender, (int64_t) 0x105E), 0x5E, 0x10, 0, 0, 0x0E, 0, 0x04, 0);
    CHECK_CMD(katherine_cmd_set_dac_vtp_fine(&g_sender, (int64_t) 0x105F), 0x5F, 0x10, 0, 0, 0x0F, 0, 0x04, 0);
    CHECK_CMD(
        katherine_cmd_set_dac_ibias_cp_pll(&g_sender, (int64_t) 0x1060), 0x60, 0x10, 0, 0, 0x10, 0, 0x04, 0);
    CHECK_CMD(katherine_cmd_set_dac_pll_vcntrl(&g_sender, (int64_t) 0x1061), 0x61, 0x10, 0, 0, 0x11, 0, 0x04, 0);
}

/* ------------------------------------------------------------------ */
/* 4. Boundary vectors for katherine_cmd_i64                           */

static void
test_i64_boundary(void)
{
    /* katherine_cmd6_i64 with value = 2^32: the payload is a fixed 4-byte
       little-endian store of the low 32 bits, so a value whose low 32
       bits are zero encodes as an all-zero payload regardless of any bits
       set above bit 31. byte[4] is not part of the payload store and
       stays at its zero-initialized value; opcode untouched at byte[6]. */
    CHECK_CMD(katherine_cmd6_i64(&g_sender, (char) 0x2A, (int64_t) 0x100000000LL), 0, 0, 0, 0, 0, 0, 0x2A, 0);

    /* katherine_cmd64_i64 with value = 2^32, taken via the set_dac_vfbk
       path (sub-index 4, the real DAC index for VFBK): byte[4] keeps the
       sub-index katherine_cmd64_i64 had already written before calling
       into the payload store, since the store never reaches past
       byte[3]. The datagram still addresses VFBK, not some other DAC. */
    CHECK_CMD(katherine_cmd_set_dac_vfbk(&g_sender, (int64_t) 0x100000000LL), 0, 0, 0, 0, 0x04, 0, 0x04, 0);

    /* katherine_cmd6_i64 with value = 2^40: same boundary as 2^32, one
       nibble further up -- still only the low 32 bits reach the wire, so
       byte[4] and byte[5] both stay zero and the opcode is untouched. */
    CHECK_CMD(katherine_cmd6_i64(&g_sender, (char) 0x2A, (int64_t) 0x10000000000LL), 0, 0, 0, 0, 0, 0, 0x2A, 0);

    /* katherine_cmd6_i64 with value = INT64_MAX (0x7FFFFFFFFFFFFFFF): the
       low 32 bits are all set, so the payload is 0xFFFFFFFF; the opcode
       at byte[6] is untouched since the store never reaches past
       byte[3]. */
    CHECK_CMD(katherine_cmd6_i64(&g_sender, (char) 0x2A, (int64_t) INT64_MAX), 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0x2A, 0);

    /* katherine_cmd6_i64 with value = -1: the store reinterprets the
       value's low 32 bits as their two's complement bit pattern, so -1
       encodes identically to 0xFFFFFFFF instead of being silently dropped
       -- the wire field is a fixed-width word, not a signed quantity the
       encoder can decline to represent. */
    CHECK_CMD(katherine_cmd6_i64(&g_sender, (char) 0x2A, (int64_t) -1), 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0x2A, 0);
}

/* ------------------------------------------------------------------ */
/* 5. katherine_general_config_word(): the GeneralConfig sensor register */
/* built from named bits of katherine_config_t, encoded via            */
/* katherine_cmd64_i64 exactly as katherine_configure() (config.c)     */
/* sends it: opcode CMD_TYPE_SENSOR_REGISTER_SETTING, sub-index         */
/* TPX3_REG_GENERAL_CONFIG.                                             */

static void
test_general_config_word(void)
{
    katherine_config_t config;
    memset(&config, 0, sizeof(config));

    /* All-zero config: AckCommand_en and Fast_lo_en are pinned on (no
       corresponding field), Gray_count_en is on because gray_disable is
       false, and Polarity is high because polarity_holes is false
       (electrons) -- 0x59. This is the value the preexisting 0x58-preset
       code also produced for these inputs: no regression here. */
    CHECK_CMD(katherine_cmd64_i64(&g_sender, (char) CMD_TYPE_SENSOR_REGISTER_SETTING,
                  (char) TPX3_REG_GENERAL_CONFIG, katherine_general_config_word(&config)),
        0x59, 0, 0, 0, TPX3_REG_GENERAL_CONFIG, 0, CMD_TYPE_SENSOR_REGISTER_SETTING, 0);

    /* gray_disable = true: Gray_count_en (bit 3) must clear -- 0x51. The
       preexisting code started general_setup from a 0x58 base with bit 3
       already set and only ever ORed gray_disable into it, so it kept
       producing 0x59 here regardless of this field; this is the
       newly-effective case. */
    config.gray_disable = true;
    CHECK_CMD(katherine_cmd64_i64(&g_sender, (char) CMD_TYPE_SENSOR_REGISTER_SETTING,
                  (char) TPX3_REG_GENERAL_CONFIG, katherine_general_config_word(&config)),
        0x51, 0, 0, 0, TPX3_REG_GENERAL_CONFIG, 0, CMD_TYPE_SENSOR_REGISTER_SETTING, 0);
    config.gray_disable = false;

    /* polarity_holes = true: Polarity (bit 0) clears -- 0x58. This bit
       already worked under the preexisting code too, since its base
       value started at 0; included to freeze every named bit, not only
       the one the fix changes. */
    config.polarity_holes = true;
    CHECK_CMD(katherine_cmd64_i64(&g_sender, (char) CMD_TYPE_SENSOR_REGISTER_SETTING,
                  (char) TPX3_REG_GENERAL_CONFIG, katherine_general_config_word(&config)),
        0x58, 0, 0, 0, TPX3_REG_GENERAL_CONFIG, 0, CMD_TYPE_SENSOR_REGISTER_SETTING, 0);
    config.polarity_holes = false;

    /* test_pulse_config.enabled = true: TP_en (bit 5) sets -- 0x79. Also
       already correct under the preexisting code (same zero-base
       reasoning as Polarity). */
    config.test_pulse_config.enabled = true;
    CHECK_CMD(katherine_cmd64_i64(&g_sender, (char) CMD_TYPE_SENSOR_REGISTER_SETTING,
                  (char) TPX3_REG_GENERAL_CONFIG, katherine_general_config_word(&config)),
        0x79, 0, 0, 0, TPX3_REG_GENERAL_CONFIG, 0, CMD_TYPE_SENSOR_REGISTER_SETTING, 0);
    config.test_pulse_config.enabled = false;

    /* The remaining flag combinations, freezing the whole 2^3 space the
       word derives from. The two involving gray_disable are further
       newly-effective values; the last one exercised all three bits at
       once under neither the old nor the new code until here. */
    config.gray_disable              = true;
    config.test_pulse_config.enabled = true;
    CHECK_CMD(katherine_cmd64_i64(&g_sender, (char) CMD_TYPE_SENSOR_REGISTER_SETTING,
                  (char) TPX3_REG_GENERAL_CONFIG, katherine_general_config_word(&config)),
        0x71, 0, 0, 0, TPX3_REG_GENERAL_CONFIG, 0, CMD_TYPE_SENSOR_REGISTER_SETTING, 0);

    config.test_pulse_config.enabled = false;
    config.polarity_holes            = true;
    CHECK_CMD(katherine_cmd64_i64(&g_sender, (char) CMD_TYPE_SENSOR_REGISTER_SETTING,
                  (char) TPX3_REG_GENERAL_CONFIG, katherine_general_config_word(&config)),
        0x50, 0, 0, 0, TPX3_REG_GENERAL_CONFIG, 0, CMD_TYPE_SENSOR_REGISTER_SETTING, 0);

    config.gray_disable              = false;
    config.test_pulse_config.enabled = true;
    CHECK_CMD(katherine_cmd64_i64(&g_sender, (char) CMD_TYPE_SENSOR_REGISTER_SETTING,
                  (char) TPX3_REG_GENERAL_CONFIG, katherine_general_config_word(&config)),
        0x78, 0, 0, 0, TPX3_REG_GENERAL_CONFIG, 0, CMD_TYPE_SENSOR_REGISTER_SETTING, 0);

    config.gray_disable = true;
    CHECK_CMD(katherine_cmd64_i64(&g_sender, (char) CMD_TYPE_SENSOR_REGISTER_SETTING,
                  (char) TPX3_REG_GENERAL_CONFIG, katherine_general_config_word(&config)),
        0x70, 0, 0, 0, TPX3_REG_GENERAL_CONFIG, 0, CMD_TYPE_SENSOR_REGISTER_SETTING, 0);
}


/* ------------------------------------------------------------------ */
/* 6. The acquisition-setup (0x21/0x05) trigger word.                  */

/* A self-contained sender/capture pair: katherine_acquisition_setup()
   takes a whole device, so its control session is initialized in place
   inside a device shell (never copied -- the embedded mutex must only be
   initialized and finalized, not duplicated). The capture peer never
   acknowledges, so the call is expected to come back with the receive
   timeout after its datagram -- the datagram is what is under test. */
#define PORT_SETUP_CAPTURE 42604
#define PORT_SETUP_SENDER  42605

static void
check_setup_word(katherine_device_t *dev, katherine_udp_t *capture, const katherine_trigger_t *start,
    bool delayed_start, const katherine_trigger_t *end, unsigned char b0, unsigned char b1)
{
    KT_CHECK_EQ(katherine_acquisition_setup(dev, start, delayed_start, end), -KATHERINE_E_TIMEOUT);

    unsigned char got[8];
    size_t n = sizeof(got);
    KT_CHECK_EQ(katherine_udp_recv(capture, got, &n), 0);
    KT_CHECK_EQ(n, 8);

    const unsigned char expected[8] = {b0, b1, 0, 0, 0x05, 0, CMD_TYPE_ACQUISITION_SETUP, 0};
    KT_CHECK_MEM_EQ(got, expected, 8);
}

static void
test_acquisition_setup_word(void)
{
    katherine_udp_t capture;
    katherine_device_t dev;
    KT_REQUIRE(katherine_udp_init_bound(
                   &capture, "127.0.0.1", PORT_SETUP_CAPTURE, "127.0.0.1", PORT_SETUP_SENDER, CAPTURE_TIMEOUT_MS)
        == 0);
    katherine_udp_pin_remote(&capture);
    if (katherine_udp_init_bound(
            &dev.control_socket, "127.0.0.1", PORT_SETUP_SENDER, "127.0.0.1", PORT_SETUP_CAPTURE, CAPTURE_TIMEOUT_MS)
        != 0) {
        katherine_udp_fini(&capture);
        KT_REQUIRE(false);
    }

    /* Every field at its maximum in-range value: channel 7 fills bits
       1..3 exactly, so a mask regression (channel bleeding into the edge
       or delayed-start flags) cannot hide. */
    katherine_trigger_t start = {.enabled = true, .channel = 7, .use_falling_edge = true};
    katherine_trigger_t end   = {.enabled = false, .channel = 0, .use_falling_edge = false};
    check_setup_word(&dev, &capture, &start, true, &end, 0x3F, 0x00);

    /* Out-of-range channels truncate to their low three bits instead of
       corrupting the flags above the field: 9 -> 1, 10 -> 2. */
    start = (katherine_trigger_t) {.enabled = false, .channel = 9, .use_falling_edge = false};
    end   = (katherine_trigger_t) {.enabled = true, .channel = 10, .use_falling_edge = true};
    check_setup_word(&dev, &capture, &start, false, &end, 0x02, 0x15);

    katherine_udp_fini(&dev.control_socket);
    katherine_udp_fini(&capture);
}

int
main(void)
{
    if (fixture_init() != 0) {
        fprintf(stderr, "test_cmd_encoders: fixture setup failed\n");
        return 1;
    }

    KT_RUN(test_primitive_cmd6);
    KT_RUN(test_primitive_cmd60);
    KT_RUN(test_primitive_cmd6_i64);
    KT_RUN(test_primitive_cmd64_i64);
    KT_RUN(test_primitive_cmd6_float);
    KT_RUN(test_wrappers_arg0_base);
    KT_RUN(test_wrappers_hw);
    KT_RUN(test_wrappers_arg1_i64);
    KT_RUN(test_wrappers_set_bias_settings);
    KT_RUN(test_wrappers_dac);
    KT_RUN(test_i64_boundary);
    KT_RUN(test_general_config_word);
    KT_RUN(test_acquisition_setup_word);

    fixture_fini();
    return kt_summary();
}
