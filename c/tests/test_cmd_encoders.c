/**
 * @file
 * @brief Golden byte-vector tests for the command encoders in command_interface.h.
 *
 * Wire format: every command is one 8-byte little-endian UDP datagram --
 * byte[6] carries the opcode, byte[4] a sub-index where the command has one,
 * bytes[0..3] the payload; byte[5] is the chip index and byte[7] the high
 * opcode byte, both always zero for this single-chip device generation. A
 * receiving socket is bound on loopback and a katherine_udp_t sender is
 * pointed at it; each case invokes an encoder directly and captures the
 * resulting datagram for a byte-exact comparison against a hand-written
 * expected array.
 *
 * Covers, in order:
 *   1. The five encoding primitives (katherine_cmd6, katherine_cmd60,
 *      katherine_cmd6_i64, katherine_cmd64_i64, katherine_cmd6_float), plus
 *      a couple of boundary vectors that freeze correct edge-case behavior
 *      of the nibble-packing loop in katherine_cmd_i64.
 *   2. Every ARG0 wrapper (base commands and the 16 hw_* sub-commands).
 *   3. Every ARG1 wrapper (the plain settings, set_bias_settings, and the
 *      18 set_dac_* wrappers).
 *   4. Boundary vectors that freeze known bugs in katherine_cmd_i64 so a
 *      later fix flips them in one visible place.
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

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <katherine/udp.h>

#include "command_interface.h"
#include "ktest.h"

/* ------------------------------------------------------------------ */
/* Loopback fixture: one bound capture socket, one sender pointed at   */
/* it. Shared by every test below -- these primitives never wait for   */
/* an acknowledgement, so a single send/recv pair per case is enough.  */

static int g_capture_sock = -1;
static katherine_udp_t g_sender;

static int
fixture_init(void)
{
    g_capture_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_capture_sock < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0; /* let the OS pick a free port */
    if (bind(g_capture_sock, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
        close(g_capture_sock);
        return -1;
    }

    struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
    if (setsockopt(g_capture_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        close(g_capture_sock);
        return -1;
    }

    struct sockaddr_in bound;
    socklen_t bound_len = sizeof(bound);
    if (getsockname(g_capture_sock, (struct sockaddr *) &bound, &bound_len) != 0) {
        close(g_capture_sock);
        return -1;
    }

    if (katherine_udp_init(&g_sender, 0, "127.0.0.1", ntohs(bound.sin_port), 2000) != 0) {
        close(g_capture_sock);
        return -1;
    }

    /* Pin the capture socket to the sender's source address, so a stray
       datagram from any other socket on the box can never be mistaken for
       part of this test's traffic. katherine_udp_init() above was given
       local_port 0, so the sender's socket is bound to INADDR_ANY on an
       OS-picked port; but since it only ever talks to 127.0.0.1, its
       outgoing packets leave with 127.0.0.1 as their source address. */
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);
    if (getsockname(g_sender.sock, (struct sockaddr *) &sender_addr, &sender_len) != 0) {
        katherine_udp_fini(&g_sender);
        close(g_capture_sock);
        return -1;
    }
    sender_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(g_capture_sock, (struct sockaddr *) &sender_addr, sizeof(sender_addr)) != 0) {
        katherine_udp_fini(&g_sender);
        close(g_capture_sock);
        return -1;
    }

    return 0;
}

static void
fixture_fini(void)
{
    katherine_udp_fini(&g_sender);
    close(g_capture_sock);
}

/* Captures the next datagram sent by g_sender into got[8]. The recv buffer
   is oversized to 16 bytes and the received length is required to be
   exactly 8, so an encoder that emits a short or long datagram fails here
   instead of being silently truncated or padded into a false match. Once
   the datagram is read, the socket must be empty: a single extra
   MSG_DONTWAIT recv is required to fail with EAGAIN/EWOULDBLOCK, so a
   future two-datagram encoder is caught at its own CHECK_CMD instead of
   desynchronizing every case that runs after it. */
static void
capture(unsigned char got[8])
{
    unsigned char buf[16];
    memset(buf, 0xAA, sizeof(buf));
    ssize_t n = recv(g_capture_sock, buf, sizeof(buf), 0);
    KT_CHECK_EQ(n, 8);
    memcpy(got, buf, 8);

    unsigned char drain[16];
    errno            = 0;
    ssize_t leftover = recv(g_capture_sock, drain, sizeof(drain), MSG_DONTWAIT);
    KT_CHECK(leftover == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
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

    /* value = 0xFFFFFFFF: the byte-splitting loop consumes exactly the 32
       payload bits and stops there, since its pre-iteration check sees
       value >> 32 == 0 for any input that fits in 32 bits. byte[4] is
       never written and stays at its zero-initialized value. This pins
       the high-nibble packing at the top of the 32-bit range, right below
       the byte[4]/byte[5] overrun boundary frozen in
       test_known_bugs_i64_boundary(). */
    CHECK_CMD(
        katherine_cmd6_i64(&g_sender, (char) 0x2A, (int64_t) 0xFFFFFFFF), 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0x2A, 0);
}

static void
test_primitive_cmd64_i64(void)
{
    /* value = 0x0E0F1011 -> little-endian payload 11 10 0F 0E; sub-index 0x09. */
    CHECK_CMD(katherine_cmd64_i64(&g_sender, (char) 0x04, (char) 0x09, (int64_t) 0x0E0F1011), 0x11, 0x10, 0x0F, 0x0E,
        0x09, 0, 0x04, 0);

    /* value = 0: the byte-splitting loop's condition is "value > 0", so it
       never runs at all and cmd[4] keeps the sub-index that
       katherine_cmd64_i64 had already written before calling into it. This
       is correct behavior worth freezing on its own: it is exactly what
       katherine_set_dacs() (config.c) relies on when a caller configures a
       DAC to value 0 -- the sub-index must survive so the datagram still
       addresses the right DAC instead of silently reading as DAC 0. */
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
test_known_bugs_i64_boundary(void)
{
    /* KNOWN-BUG: header-byte overrun, fixed by a later commit -- these expectations flip then. */

    /* katherine_cmd6_i64 with value = 2^32: the byte-splitting loop in
       katherine_cmd_i64 runs on "value > 0", not a fixed 4-byte count, so
       a value whose low 32 bits are zero but which is itself nonzero
       forces a fifth iteration. That iteration writes cmd[4], which for
       this primitive is otherwise unused (and stays zero for any value
       that fits in 32 bits). Observed: payload 00 00 00 00, byte[4] = 01
       (the low byte of value >> 32), opcode untouched at byte[6]. */
    CHECK_CMD(
        katherine_cmd6_i64(&g_sender, (char) 0x2A, (int64_t) 0x100000000LL), 0, 0, 0, 0, 0x01, 0, 0x2A, 0);

    /* katherine_cmd64_i64 with value = 2^32, taken via the set_dac_vfbk
       path (sub-index 4, the real DAC index for VFBK): the same fifth
       iteration now lands on byte[4], which katherine_cmd64_i64 had
       already set to the sub-index. The sub-index is silently replaced
       by the overflow byte 01, so the datagram reads as sub-index 1
       (ibias_preamp_off) instead of 4 (vfbk). Observed: payload all
       zero, byte[4] = 01, opcode 0x04 unaffected. */
    CHECK_CMD(katherine_cmd_set_dac_vfbk(&g_sender, (int64_t) 0x100000000LL), 0, 0, 0, 0, 0x01, 0, 0x04, 0);

    /* katherine_cmd6_i64 with value = 2^40: the same "value > 0" loop
       condition runs a sixth iteration once the value no longer fits in 40
       bits, writing cmd[5] too -- one byte further than the byte[4] overrun
       above. 2^40 is the smallest value for which this happens: bytes
       [0..3] hold the low 32 bits (zero here), byte[4] holds bits 32-39
       (also zero here), and the sixth iteration writes byte[5] = bits
       40-47 of the value, i.e. 0x01. Observed: payload 00 00 00 00,
       byte[4] = 00, byte[5] = 01, opcode untouched at byte[6]. */
    CHECK_CMD(
        katherine_cmd6_i64(&g_sender, (char) 0x2A, (int64_t) 0x10000000000LL), 0, 0, 0, 0, 0, 0x01, 0x2A, 0);

    /* katherine_cmd6_i64 with value = INT64_MAX (0x7FFFFFFFFFFFFFFF, all 63
       low bits set): the loop keeps splitting off one byte per iteration
       for as long as the remaining value is nonzero, so a 63-bit value
       drives it through all 8 iterations. The seventh iteration overwrites
       cmd[6], which katherine_cmd6_i64 had already set to the opcode, and
       the eighth writes cmd[7] -- every payload byte and the opcode itself
       end up clobbered. Observed: all eight bytes overwritten, so byte[6]
       reads as 0xFF instead of the requested opcode 0x2A. */
    CHECK_CMD(katherine_cmd6_i64(&g_sender, (char) 0x2A, (int64_t) INT64_MAX), 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0x7F);

    /* katherine_cmd6_i64 with a negative value: the loop condition is
       "value > 0", so a negative value never enters the loop at all and
       every payload byte stays at its zero-initialized value -- the
       argument is silently dropped rather than encoded. Observed:
       payload 00 00 00 00, opcode untouched at byte[6]. */
    CHECK_CMD(katherine_cmd6_i64(&g_sender, (char) 0x2A, (int64_t) -1), 0, 0, 0, 0, 0, 0, 0x2A, 0);
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
    KT_RUN(test_known_bugs_i64_boundary);

    fixture_fini();
    return kt_summary();
}
