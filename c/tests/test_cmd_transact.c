/**
 * @file
 * @brief Correlation of command responses with the requests that provoked them.
 *
 * The readout answers an acknowledged command with exactly eight bytes, all
 * zero but for byte 6, which repeats the operation code of the request. Until
 * this facility existed the library read whatever datagram arrived next and
 * called it the answer, so one stray -- a late response of an earlier
 * command, an unsolicited datagram, a command of its own delivered back to it
 * -- shifted every response of the session onto the wrong request, silently
 * and for good.
 *
 * The cases below drive the exchange primitives of protocol/cmd_interface.h
 * against a mock readout on loopback, so that the peer's behaviour is chosen
 * byte for byte rather than merely observed. What is asserted:
 *
 *   - a response whose identifier matches is found behind a stray, and the
 *     stray is counted rather than silently dropped;
 *   - a peer that never sends the matching response exhausts a bounded
 *     budget and the call returns instead of hanging;
 *   - a datagram whose length is not eight is rejected as malformed;
 *   - the identifiers the readout firmware substitutes for its own -- 0x24
 *     answered under 0x22, 0x14 answered twenty-two times under 0x0F -- are
 *     accepted by default and refused in strict mode, which is why strict
 *     mode is opt-in;
 *   - the commands the firmware never answers are refused a wait outright,
 *     because a wait there would stall every acquisition;
 *   - the flush that opens a transaction consumes what an earlier exchange
 *     left behind and does so without blocking.
 *
 * @author Petr Mánek
 * @date 26.8.26
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
#include <time.h>

// katherine/katherine.h must precede kthread.h: on Windows it transitively
// pulls in <winsock2.h> (via udp_win.h) ahead of the <windows.h> that
// kthread.h includes, and the reverse order does not compile under the
// Windows SDK. Same reasoning as test_tp.c.
#include <katherine/katherine.h>
#ifdef KATHERINE_TEST_EMULATOR
#include <katherine/emulator.h>
#endif

#include "protocol/cmd_builder.h"
#include "protocol/cmd_interface.h"
#include "kthread.h"
#include "msleep.h"
#include "ktest.h"

/* ------------------------------------------------------------------ */
/* Endpoints. High, uncommon ports of their own, distinct from every other
   fixed set registered in this tree (test_udp_pinning.c's 42555-42557,
   test_cmd_encoders.c's 42600/42601, bench_udp.c's 473xx, the ksim daemon's
   1555/1556): nothing here claims a global resource, so the test needs no
   exclusive slot among the others. */

#define PORT_CLIENT       42610
#define PORT_MOCK         42611
#define PORT_SLOW_CLIENT  42612
#define PORT_SLOW_MOCK    42613

/* Receive timeout of the session under test. Every datagram a case expects
   is already queued by the time the call under test runs -- loopback
   delivery completes before katherine_udp_send_exact() returns -- so this
   only bounds the cases that deliberately wait for something that never
   comes, and is kept short for that reason. */
#define CLIENT_TIMEOUT_MS 50

/* The mock readout waits for a command only on the threaded case, where it
   arrives within microseconds; this is headroom against scheduling jitter,
   not a value the test expects to wait out. */
#define MOCK_TIMEOUT_MS   5000

/* The non-blocking flush is checked against a session whose timeout it must
   not spend: a blocking receive on the empty socket of this second pair
   would cost whole seconds, which time(NULL)'s one-second resolution can
   tell apart from returning at once. */
#define SLOW_TIMEOUT_MS   3000
#define NONBLOCKING_S     1.0

/* Retries allowed while waiting for two datagrams to reach the socket, and
   the pause between them. Ten attempts of 20 ms bound the wait at 0.2 s,
   far above any loopback delivery and far below the test's own timeout. */
#define FLUSH_ATTEMPTS    10
#define FLUSH_ATTEMPT_MS  20

/* The stray budget is spent on datagrams that are already queued, so
   spending all of it costs no receive timeout at all; this bound catches a
   budget that waits instead of a budget that reads. */
#define BOUNDED_WAIT_S    2.0

/* Number of response datagrams the readout firmware sends for the all-DAC
   scan (0x14), each carrying the single-DAC-scan identifier 0x0F. */
#define DAC_SCAN_REPLIES  22

static katherine_udp_t g_client;
static katherine_udp_t g_mock;
static katherine_udp_t g_slow_client;
static katherine_udp_t g_slow_mock;

/* Brings up both pairs. The client sessions are pinned, as a device's own
   are (katherine_device_init()), so that the datagrams the cases below step
   over are rejected by the correlation under test rather than by the
   address check underneath it. The mock sessions stay unpinned, which is
   the server behaviour that makes them answer whoever asked last. */
static int
endpoints_init(void)
{
    int res;

    if ((res = katherine_udp_init_bound(&g_client, "127.0.0.1", PORT_CLIENT, "127.0.0.1", PORT_MOCK, CLIENT_TIMEOUT_MS))
        != 0) {
        goto err_client;
    }
    katherine_udp_pin_remote(&g_client);

    if ((res = katherine_udp_init_bound(&g_mock, "127.0.0.1", PORT_MOCK, "127.0.0.1", PORT_CLIENT, MOCK_TIMEOUT_MS))
        != 0) {
        goto err_mock;
    }

    if ((res = katherine_udp_init_bound(
             &g_slow_client, "127.0.0.1", PORT_SLOW_CLIENT, "127.0.0.1", PORT_SLOW_MOCK, SLOW_TIMEOUT_MS))
        != 0) {
        goto err_slow_client;
    }
    katherine_udp_pin_remote(&g_slow_client);

    if ((res = katherine_udp_init_bound(
             &g_slow_mock, "127.0.0.1", PORT_SLOW_MOCK, "127.0.0.1", PORT_SLOW_CLIENT, MOCK_TIMEOUT_MS))
        != 0) {
        goto err_slow_mock;
    }

    return 0;

err_slow_mock:
    katherine_udp_fini(&g_slow_client);
err_slow_client:
    katherine_udp_fini(&g_mock);
err_mock:
    katherine_udp_fini(&g_client);
err_client:
    return res;
}

static void
endpoints_fini(void)
{
    katherine_udp_fini(&g_slow_mock);
    katherine_udp_fini(&g_slow_client);
    katherine_udp_fini(&g_mock);
    katherine_udp_fini(&g_client);
}

/* ------------------------------------------------------------------ */
/* Mock readout helpers. */

/* Sends one well-formed response datagram: the identifier in byte 6 and a
   little-endian payload word in bytes 0 to 3, exactly as the readout
   encodes a response that carries data. */
static void
mock_reply(katherine_udp_t *mock, uint8_t reply_id, uint32_t payload)
{
    unsigned char crd[KATHERINE_CMD_CRD_SIZE] = {0};

    crd[0]                         = (unsigned char) (payload & 0xFFu);
    crd[1]                         = (unsigned char) ((payload >> 8) & 0xFFu);
    crd[2]                         = (unsigned char) ((payload >> 16) & 0xFFu);
    crd[3]                         = (unsigned char) ((payload >> 24) & 0xFFu);
    crd[KATHERINE_CMD_OPCODE_BYTE] = reply_id;

    KT_CHECK_EQ(katherine_udp_send_exact(mock, crd, sizeof(crd)), 0);
}

static uint32_t
load_payload(const char *crd)
{
    const unsigned char *b = (const unsigned char *) crd;
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
}

/* Requires the session to hold nothing more: a short-timeout receive
   reporting -KATHERINE_E_TIMEOUT stands in for the non-blocking drain check
   a raw socket would do with MSG_DONTWAIT. */
static void
expect_quiet(katherine_udp_t *u)
{
    unsigned char buf[64];
    size_t count = sizeof(buf);

    KT_CHECK_EQ(katherine_udp_recv(u, buf, &count), -KATHERINE_E_TIMEOUT);
}

/* ------------------------------------------------------------------ */
/* a) The two tables that state what the readout answers, and under which
      identifier. Both are read off the firmware's command dispatcher, and
      both are load-bearing: a wrong entry either stalls a command that is
      never answered or rejects the answer of one that is.               */

static void
test_reply_identifier_table(void)
{
    /* The firmware answers the trigger-generator read-back with the
       acquisition-unit read-back's identifier, and the all-DAC scan with
       the single-DAC scan's -- never their own. */
    KT_CHECK_EQ(katherine_cmd_reply_id(CMD_TYPE_TRIGGER_GENERATOR_SETUP_READ), CMD_TYPE_GET_ACQUISITION_UNIT_DATA);
    KT_CHECK_EQ(katherine_cmd_reply_id(CMD_TYPE_GET_ALL_DAC_SCAN), CMD_TYPE_INTERNAL_DAC_SCAN);

    /* Every other command answers under its own operation code. */
    KT_CHECK_EQ(katherine_cmd_reply_id(CMD_TYPE_GET_READOUT_STATUS), CMD_TYPE_GET_READOUT_STATUS);
    KT_CHECK_EQ(katherine_cmd_reply_id(CMD_TYPE_HW_COMMAND_START), CMD_TYPE_HW_COMMAND_START);
    KT_CHECK_EQ(katherine_cmd_reply_id(CMD_TYPE_SET_ALL_PIXEL_CONFIG), CMD_TYPE_SET_ALL_PIXEL_CONFIG);
}

static void
test_unacknowledged_command_table(void)
{
    /* The three the acquisition path depends on: the readout acts on them
       and says nothing. */
    KT_CHECK(!katherine_cmd_is_acknowledged(CMD_TYPE_ACQUISITION_START));
    KT_CHECK(!katherine_cmd_is_acknowledged(CMD_TYPE_SEQ_READOUT_START));
    KT_CHECK(!katherine_cmd_is_acknowledged(CMD_TYPE_ACQUISITION_STOP));

    /* Documented as a handshake, acknowledged by nothing in the firmware. */
    KT_CHECK(!katherine_cmd_is_acknowledged(CMD_TYPE_INTERFACE_SELECTION));

    KT_CHECK(katherine_cmd_is_acknowledged(CMD_TYPE_SET_ALL_PIXEL_CONFIG));
    KT_CHECK(katherine_cmd_is_acknowledged(CMD_TYPE_HW_COMMAND_START));
    KT_CHECK(katherine_cmd_is_acknowledged(CMD_TYPE_GET_READOUT_STATUS));
}

/* b) Asking to wait for one of those is a programming error, reported
      without touching the socket -- the alternative is an acquisition that
      never starts, because the wait can only ever time out.              */

static void
test_unacknowledged_commands_never_wait(void)
{
    KT_CHECK_EQ(katherine_cmd_wait_ack(&g_client, CMD_TYPE_ACQUISITION_START), -KATHERINE_E_INVAL);
    KT_CHECK_EQ(katherine_cmd_wait_ack(&g_client, CMD_TYPE_SEQ_READOUT_START), -KATHERINE_E_INVAL);
    KT_CHECK_EQ(katherine_cmd_wait_ack(&g_client, CMD_TYPE_ACQUISITION_STOP), -KATHERINE_E_INVAL);

    /* The transaction primitive refuses before it sends, so the peer sees
       nothing at all: a caller who reaches for it by mistake gets a
       diagnosis, not a half-issued command. */
    katherine_cmd_t cmd = katherine_cmd_create((uint8_t) CMD_TYPE_ACQUISITION_STOP);
    KT_CHECK_EQ(katherine_cmd_transact(&g_client, cmd.b, sizeof(cmd.b), NULL), -KATHERINE_E_INVAL);
    expect_quiet(&g_mock);

    /* A datagram too short to carry an operation code is no command. */
    KT_CHECK_EQ(katherine_cmd_transact(&g_client, cmd.b, KATHERINE_CMD_CRD_SIZE - 1, NULL), -KATHERINE_E_INVAL);
    expect_quiet(&g_mock);
}

/* c) The heart of it: the response that belongs to the request is found
      behind one that does not, and the one stepped over is counted.      */

static void
test_correlates_past_stray(void)
{
    const uint64_t before = g_client.stray_command_responses;
    char crd[KATHERINE_CMD_CRD_SIZE];

    mock_reply(&g_mock, (uint8_t) CMD_TYPE_GET_HW_READOUT_TEMPERATURE, 0x11111111u);
    mock_reply(&g_mock, (uint8_t) CMD_TYPE_GET_READOUT_STATUS, 0xDEADBEEFu);

    KT_CHECK_EQ(katherine_cmd_wait_ack_crd(&g_client, (uint8_t) CMD_TYPE_GET_READOUT_STATUS, crd), 0);
    KT_CHECK_EQ((unsigned char) crd[KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_GET_READOUT_STATUS);
    KT_CHECK_EQ(load_payload(crd), 0xDEADBEEFu);
    KT_CHECK_EQ(g_client.stray_command_responses - before, 1);

    expect_quiet(&g_client);
}

/* d) A peer that talks without ever answering must not hold the caller. The
      budget is spent on datagrams that are already queued, so the call
      returns long before the receive timeout would have expired, and the
      datagrams it never reached are still there afterwards.              */

static void
test_stray_budget_bounds_the_wait(void)
{
    const uint64_t before = g_client.stray_command_responses;
    const uint32_t extra  = 4;

    for (uint32_t i = 0; i < KATHERINE_CMD_MAX_STRAY_DISCARDS + extra; ++i) {
        mock_reply(&g_mock, (uint8_t) CMD_TYPE_GET_HW_READOUT_TEMPERATURE, i);
    }

    time_t started = time(NULL);
    KT_CHECK_EQ(katherine_cmd_wait_ack(&g_client, (uint8_t) CMD_TYPE_GET_READOUT_STATUS), -KATHERINE_E_STRAY);
    KT_CHECK(difftime(time(NULL), started) <= BOUNDED_WAIT_S);

    KT_CHECK_EQ(g_client.stray_command_responses - before, KATHERINE_CMD_MAX_STRAY_DISCARDS);

    katherine_cmd_drain(&g_client);
    expect_quiet(&g_client);
}

/* e) The readout's command socket carries nothing but eight-byte responses,
      so any other length is the peer violating the wire format rather than
      a stray to be stepped over. Both a short and an oversized datagram are
      consumed -- the session is not left wedged behind them -- and reported.
                                                                          */

static void
test_malformed_response_rejected(void)
{
    static const unsigned char SHORT[4] = {0, 0, 0, 0};
    unsigned char oversized[KATHERINE_CMD_CRD_SIZE + 4];

    KT_CHECK_EQ(katherine_udp_send_exact(&g_mock, SHORT, sizeof(SHORT)), 0);
    KT_CHECK_EQ(katherine_cmd_wait_ack(&g_client, (uint8_t) CMD_TYPE_GET_READOUT_STATUS), -KATHERINE_E_BAD_CRD);

    memset(oversized, 0, sizeof(oversized));
    oversized[KATHERINE_CMD_OPCODE_BYTE] = (unsigned char) CMD_TYPE_GET_READOUT_STATUS;
    KT_CHECK_EQ(katherine_udp_send_exact(&g_mock, oversized, sizeof(oversized)), 0);
    KT_CHECK_EQ(katherine_cmd_wait_ack(&g_client, (uint8_t) CMD_TYPE_GET_READOUT_STATUS), -KATHERINE_E_BAD_CRD);

    expect_quiet(&g_client);
}

/* f) The firmware's own substitutions are not errors. Rejecting them would
      break the very hardware this library talks to, which is why they are
      accepted by default and are not counted as strays.                  */

static void
test_documented_reply_identifiers_accepted(void)
{
    uint64_t before = g_client.stray_command_responses;
    char crd[KATHERINE_CMD_CRD_SIZE];

    /* The trigger-generator read-back, answered under the acquisition-unit
       read-back's identifier. */
    mock_reply(&g_mock, (uint8_t) CMD_TYPE_GET_ACQUISITION_UNIT_DATA, 0xCAFEu);
    KT_CHECK_EQ(katherine_cmd_wait_ack_crd(&g_client, (uint8_t) CMD_TYPE_TRIGGER_GENERATOR_SETUP_READ, crd), 0);
    KT_CHECK_EQ(load_payload(crd), 0xCAFEu);
    KT_CHECK_EQ(g_client.stray_command_responses - before, 0);

    /* The all-DAC scan, answered twenty-two times under the single-DAC
       scan's identifier: the first datagram is the answer to the request. */
    for (uint32_t i = 0; i < DAC_SCAN_REPLIES; ++i) {
        mock_reply(&g_mock, (uint8_t) CMD_TYPE_INTERNAL_DAC_SCAN, i);
    }

    before = g_client.stray_command_responses;
    KT_CHECK_EQ(katherine_cmd_wait_ack_crd(&g_client, (uint8_t) CMD_TYPE_GET_ALL_DAC_SCAN, crd), 0);
    KT_CHECK_EQ(load_payload(crd), 0);
    KT_CHECK_EQ(g_client.stray_command_responses - before, 0);

    /* The twenty-one left over are what the flush of the next transaction
       exists for: without it they would be read as its answers, one command
       out of step, for the rest of the session. */
    before = g_client.stray_command_responses;
    katherine_cmd_drain(&g_client);
    KT_CHECK_EQ(g_client.stray_command_responses - before, DAC_SCAN_REPLIES - 1);
    expect_quiet(&g_client);
}

/* g) Strict mode is the same correlation without the firmware allowances: it
      requires the identifier to be the request's own operation code. It
      therefore rejects exactly what the case above accepts, which is why it
      is opt-in and stays off until measured against hardware.            */

static void
test_strict_mode_rejects_documented_reply_identifiers(void)
{
    const uint64_t before = g_client.stray_command_responses;

    katherine_udp_set_strict_ack(&g_client, true);

    mock_reply(&g_mock, (uint8_t) CMD_TYPE_GET_ACQUISITION_UNIT_DATA, 0xCAFEu);
    KT_CHECK_EQ(katherine_cmd_wait_ack(&g_client, (uint8_t) CMD_TYPE_TRIGGER_GENERATOR_SETUP_READ),
        -KATHERINE_E_TIMEOUT);
    KT_CHECK_EQ(g_client.stray_command_responses - before, 1);

    /* A peer that does echo the operation code satisfies strict mode. */
    mock_reply(&g_mock, (uint8_t) CMD_TYPE_TRIGGER_GENERATOR_SETUP_READ, 1);
    KT_CHECK_EQ(katherine_cmd_wait_ack(&g_client, (uint8_t) CMD_TYPE_TRIGGER_GENERATOR_SETUP_READ), 0);

    katherine_udp_set_strict_ack(&g_client, false);
    expect_quiet(&g_client);
}

/* h) The flush that opens a transaction: it consumes what an earlier
      exchange left queued, and it must never spend a receive timeout doing
      so on an empty socket -- every command of the library would otherwise
      pay for it.                                                          */

static void
test_flush_consumes_without_blocking(void)
{
    const uint64_t before = g_slow_client.stray_command_responses;

    mock_reply(&g_slow_mock, (uint8_t) CMD_TYPE_GET_HW_READOUT_TEMPERATURE, 1);
    mock_reply(&g_slow_mock, (uint8_t) CMD_TYPE_GET_HW_READOUT_TEMPERATURE, 2);

    /* A send returning is not a delivery: the loopback of some platforms
       queues the datagram a moment later, and a flush that cannot block is
       entitled to find the socket still empty. So the flush is repeated
       until both have been accounted for, with every single call required
       to return at once -- which is the property under test -- and the
       whole wait bounded so a flush that consumes nothing still fails. */
    time_t started = time(NULL);
    for (int attempt = 0; attempt < FLUSH_ATTEMPTS; ++attempt) {
        time_t call = time(NULL);
        katherine_cmd_drain(&g_slow_client);
        KT_CHECK(difftime(time(NULL), call) <= NONBLOCKING_S);
        if (g_slow_client.stray_command_responses - before >= 2) break;
        katherine_msleep(FLUSH_ATTEMPT_MS);
    }
    KT_CHECK(difftime(time(NULL), started) <= NONBLOCKING_S * FLUSH_ATTEMPTS);
    KT_CHECK_EQ(g_slow_client.stray_command_responses - before, 2);

    /* Again, on the socket it has just emptied. */
    started = time(NULL);
    katherine_cmd_drain(&g_slow_client);
    KT_CHECK(difftime(time(NULL), started) <= NONBLOCKING_S);
    KT_CHECK_EQ(g_slow_client.stray_command_responses - before, 2);
}

/* i) The whole primitive, against a readout that behaves like the real one:
      a response of an earlier exchange is still queued when the transaction
      starts, and the peer prefixes its answer with a datagram of its own.
      Both are stepped over and counted, and the answer that comes back is
      the one this request provoked.

      The mock has to run on its own thread: the flush, the send and the
      receive happen inside one call, so the test cannot step in between
      them the way it can in the inline cases above.                       */

static unsigned char g_captured[KATHERINE_CMD_CRD_SIZE];

static void *
mock_readout(void *arg)
{
    (void) arg;

    size_t n = sizeof(g_captured);
    if (katherine_udp_recv(&g_mock, g_captured, &n) != 0 || n != sizeof(g_captured)) {
        fprintf(stderr, "mock readout: unexpected datagram\n");
        return NULL;
    }

    /* One datagram belonging to no request in flight, then the answer. */
    mock_reply(&g_mock, (uint8_t) CMD_TYPE_GET_ADC_VOLTAGE, 0x55555555u);
    mock_reply(&g_mock, g_captured[KATHERINE_CMD_OPCODE_BYTE], 0x0BADF00Du);
    return NULL;
}

static void
test_transact_flushes_sends_and_correlates(void)
{
    const uint64_t before = g_client.stray_command_responses;
    char crd[KATHERINE_CMD_CRD_SIZE];
    kthread_t thread;

    /* Left behind by an earlier exchange, before the transaction starts. */
    mock_reply(&g_mock, (uint8_t) CMD_TYPE_GET_HW_READOUT_TEMPERATURE, 0x99999999u);

    memset(g_captured, 0xAA, sizeof(g_captured));
    KT_REQUIRE(kthread_start(&thread, mock_readout, NULL) == 0);

    katherine_cmd_t cmd = katherine_cmd_create((uint8_t) CMD_TYPE_GET_READOUT_STATUS);
    KT_CHECK_EQ(katherine_cmd_transact(&g_client, cmd.b, sizeof(cmd.b), crd), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);

    KT_CHECK_MEM_EQ(g_captured, cmd.b, sizeof(cmd.b));
    KT_CHECK_EQ((unsigned char) crd[KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_GET_READOUT_STATUS);
    KT_CHECK_EQ(load_payload(crd), 0x0BADF00Du);

    /* One stale response flushed, one stray stepped over. */
    KT_CHECK_EQ(g_client.stray_command_responses - before, 2);
    expect_quiet(&g_client);
}

#ifdef KATHERINE_TEST_EMULATOR
/* j) The emulated readout reproduces both substitutions, so the tolerance
      above is checked against a model of the peer rather than only against
      the same table read twice. Driven in memory, with no sockets: the
      emulator takes command datagrams and hands back response datagrams.

      Both commands must also stay out of the unknown-opcode count. The
      firmware's dispatcher has no default branch, so an opcode it does not
      recognize gets no reply at all -- and these two it does recognize, which
      is exactly the divergence that once made the emulator answer commands
      the real readout ignores.                                            */

static void
test_emulated_readout_reproduces_reply_identifiers(void)
{
    katherine_emu_t emu;
    uint8_t crd[KATHERINE_EMU_CRD_SIZE];
    size_t len;

    KT_REQUIRE(katherine_emu_init(&emu, NULL) == 0);

    katherine_cmd_t read_back = katherine_cmd_create((uint8_t) CMD_TYPE_TRIGGER_GENERATOR_SETUP_READ);
    KT_CHECK_EQ(katherine_emu_cmd_in(&emu, read_back.b, sizeof(read_back.b)), 0);
    KT_CHECK_EQ(katherine_emu_crd_out(&emu, crd, &len), 0);
    KT_CHECK_EQ(len, KATHERINE_EMU_CRD_SIZE);
    KT_CHECK_EQ(crd[KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_GET_ACQUISITION_UNIT_DATA);
    KT_CHECK_EQ(katherine_emu_crd_out(&emu, crd, &len), -KATHERINE_E_TIMEOUT);

    katherine_cmd_t scan = katherine_cmd_create((uint8_t) CMD_TYPE_GET_ALL_DAC_SCAN);
    KT_CHECK_EQ(katherine_emu_cmd_in(&emu, scan.b, sizeof(scan.b)), 0);
    for (uint32_t i = 0; i < DAC_SCAN_REPLIES; ++i) {
        KT_CHECK_EQ(katherine_emu_crd_out(&emu, crd, &len), 0);
        KT_CHECK_EQ(crd[KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_INTERNAL_DAC_SCAN);
    }
    KT_CHECK_EQ(katherine_emu_crd_out(&emu, crd, &len), -KATHERINE_E_TIMEOUT);

    KT_CHECK_EQ(katherine_emu_unknown_cmd_count(&emu), 0);

    katherine_emu_fini(&emu);
}
#endif /* KATHERINE_TEST_EMULATOR */

/* ------------------------------------------------------------------ */

int
main(void)
{
    int res = endpoints_init();
    if (res != 0) {
        printf("1..0 # SKIP cannot bind the loopback endpoints: %s\n", katherine_strerror(res));
        return 77;
    }

    KT_RUN(test_reply_identifier_table);
    KT_RUN(test_unacknowledged_command_table);
    KT_RUN(test_unacknowledged_commands_never_wait);
    KT_RUN(test_correlates_past_stray);
    KT_RUN(test_stray_budget_bounds_the_wait);
    KT_RUN(test_malformed_response_rejected);
    KT_RUN(test_documented_reply_identifiers_accepted);
    KT_RUN(test_strict_mode_rejects_documented_reply_identifiers);
    KT_RUN(test_flush_consumes_without_blocking);
    KT_RUN(test_transact_flushes_sends_and_correlates);
#ifdef KATHERINE_TEST_EMULATOR
    KT_RUN(test_emulated_readout_reproduces_reply_identifiers);
#endif

    endpoints_fini();
    return kt_summary();
}
