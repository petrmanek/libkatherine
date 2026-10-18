/**
 * \file
 * \brief Finding readouts by asking them to identify themselves.
 *
 * The broadcast itself needs a network, so what is checked here is everything
 * around it: the command that goes out carries the reply port where the
 * firmware reads it, an answer is turned into the address it came from,
 * several answers are collected from several senders, and the two conditions
 * that must not be mistaken for a readout -- a request delivered back to its
 * own sender, and a reply port the firmware would ignore -- are refused.
 *
 * Aimed at a unicast loopback address, which the broadcast option permits and
 * which makes the mock readouts addressable. The broadcast delivery itself is
 * not exercised here: a datagram to the loopback broadcast address does not
 * reach a socket bound to a particular 127/8 address, so that part waits for
 * a network.
 *
 * \author Petr Mánek
 * \date 18.9.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

// Must precede every include: msleep.h needs nanosleep(), which glibc hides
// under a strict -std= unless _POSIX_C_SOURCE is set before the first libc
// header resolves feature-test macros. Same reasoning as test_cmd_transact.c.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// katherine/katherine.h must precede kthread.h on Windows; see
// test_cmd_transact.c, which documents the include order.
#include <katherine/katherine.h>

#include "protocol/cmd_builder.h"
#include "protocol/cmd_interface.h"
#include "kports.h"
#include "msleep.h"
#include "kthread.h"
#include "ktest.h"

// The readouts answer on this port, which must be at least
// KATHERINE_DISCOVER_MIN_PORT for the firmware to honour it.
#define REPLY_PORT      (KPORTS_DISCOVER + 0)

// Where the mock readouts listen. The discovery sends to the readout's
// standard control port, so each mock binds that port on an address of its
// own -- which is what loopback's whole 127/8 is for.
#define READOUT_PORT    1555

#define MOCK_TIMEOUT_MS 5000
#define DISCOVER_MS     300

// How long the unsolicited answer waits for the discovery to bind its reply
// port. Generous against scheduling jitter and well inside the collection
// window above.
#define LATE_ANSWER_MS  30

// Two readouts on two addresses, and the identifier words they report -- which
// the wire carries and nothing decodes any more. Every
// word has a nonzero low nibble, without which it would be indistinguishable
// from the request echoed back.
// The discovery runs on the host address and binds the standard port there
// to catch a legacy answer, so no mock may hold it: the readouts live on
// addresses of their own, which is what loopback's whole 127/8 is for.
#define HOST_ADDRESS    "127.0.0.1"
#define ADDRESS_A       "127.0.0.2"
#define ADDRESS_B       "127.0.0.3"

/* What a discovery reports: the address names the link as well as the
 * readout, which is the form katherine_device_init() takes back. */
#define FOUND_A         "udp://" ADDRESS_A
#define FOUND_B         "udp://" ADDRESS_B
#define CHIP_A          0x00001231 /* A3-W0018: letter 1, digit 3, wafer 0x012 */
#define CHIP_B          0x00002342 /* B4-W0035 */

typedef struct mock {
    const char *address;
    uint32_t chip_id;
    // A legacy firmware does not read the reply port out of the request and
    // answers on the standard one, which is the whole bit a discovery
    // reports.
    bool legacy;
    katherine_udp_t udp;
    bool up;
    uint8_t captured[KATHERINE_CMD_CRD_SIZE];
    bool got_request;
} mock_t;

static mock_t g_mocks[2] = {
    {ADDRESS_A, CHIP_A, false, {0}, false, {0}, false},
    {ADDRESS_B, CHIP_B, true, {0}, false, {0}, false},
};

// One readout's answer: its identifier word in the payload, under the
// echo-chip-id identifier.
static void
mock_answer(mock_t *mock)
{
    uint8_t crd[KATHERINE_CMD_CRD_SIZE] = {0};

    crd[0]                         = (uint8_t) (mock->chip_id & 0xFFu);
    crd[1]                         = (uint8_t) ((mock->chip_id >> 8) & 0xFFu);
    crd[2]                         = (uint8_t) ((mock->chip_id >> 16) & 0xFFu);
    crd[3]                         = (uint8_t) ((mock->chip_id >> 24) & 0xFFu);
    crd[KATHERINE_CMD_OPCODE_BYTE] = (uint8_t) CMD_TYPE_ECHO_CHIP_ID;
    (void) katherine_udp_send_exact(&mock->udp, crd, sizeof(crd));
}

// A readout that answers unsolicited, a moment after the discovery has bound
// its reply port. Used for the second address, which no unicast request
// reaches: the answer would be dropped if it were sent before the port
// existed.
static void *
mock_late_answer(void *arg)
{
    mock_t *mock = (mock_t *) arg;

    katherine_msleep(LATE_ANSWER_MS);
    mock_answer(mock);

    return NULL;
}

// One mock readout: receives the discovery command, records it, and answers
// on the port the command named, as the firmware does.
static void *
mock_readout(void *arg)
{
    mock_t *mock = (mock_t *) arg;

    if (katherine_udp_recv_exact(&mock->udp, mock->captured, KATHERINE_CMD_CRD_SIZE) != 0) {
        return NULL;
    }
    mock->got_request = true;

    const uint16_t named = (uint16_t) ((uint16_t) mock->captured[0] | ((uint16_t) mock->captured[1] << 8));
    const uint16_t port  = mock->legacy ? READOUT_PORT : named;
    if (katherine_udp_set_remote(&mock->udp, HOST_ADDRESS, port) != 0) return NULL;

    mock_answer(mock);
    return NULL;
}

// Brings up as many mocks as asked for, reporting how many came up: the
// second address may not be configured on this host, which is a skip rather
// than a failure.
static size_t
mocks_init(size_t count)
{
    size_t up = 0;

    for (size_t i = 0; i < count; ++i) {
        g_mocks[i].got_request = false;
        memset(g_mocks[i].captured, 0xAA, sizeof(g_mocks[i].captured));

        if (katherine_udp_init_bound(&g_mocks[i].udp, g_mocks[i].address, READOUT_PORT, HOST_ADDRESS,
                REPLY_PORT, MOCK_TIMEOUT_MS)
            != 0) {
            break;
        }
        g_mocks[i].up = true;
        ++up;
    }

    return up;
}

static void
mocks_fini(size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (g_mocks[i].up) katherine_udp_fini(&g_mocks[i].udp);
        g_mocks[i].up = false;
    }
}

// ------------------------------------------------------------------

// a) One readout, aimed at its own address: the request carries the reply
// port, and the answer becomes the address it came from.
static void
test_a_one_readout_is_recorded_under_its_address(void)
{
    katherine_discovered_t found[4];
    size_t count = 0;
    kthread_t thread;

    if (mocks_init(1) != 1) {
        printf("# SKIP cannot bind %s:%d\n", ADDRESS_A, READOUT_PORT);
        return;
    }

    KT_REQUIRE(kthread_start(&thread, mock_readout, &g_mocks[0]) == 0);

    count = 1;
    KT_CHECK_EQ(katherine_device_discover_udp(ADDRESS_A, REPLY_PORT, DISCOVER_MS, found, &count), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);

    KT_CHECK(g_mocks[0].got_request);
    KT_CHECK_EQ(g_mocks[0].captured[KATHERINE_CMD_OPCODE_BYTE], CMD_TYPE_ECHO_CHIP_ID);

    // The reply port in the first two bytes, little-endian, where the
    // firmware reads it.
    KT_CHECK_EQ(g_mocks[0].captured[0], REPLY_PORT & 0xFF);
    KT_CHECK_EQ(g_mocks[0].captured[1], (REPLY_PORT >> 8) & 0xFF);

    KT_REQUIRE(count == 1);
    KT_CHECK_STR_EQ(found[0].address, FOUND_A);

    // It answered on the port the request named, so it read the request.
    KT_CHECK(!found[0].legacy);

    mocks_fini(1);
}

// b) Two answers from two addresses, each recorded under its own -- which is
// the whole point of a discovery and the thing one mock cannot show.
//
// Both answers are provoked by one request, but only the first mock receives
// it: a datagram to the loopback broadcast address is not delivered to a
// socket bound to a particular 127/8 address, so the second mock answers
// unsolicited from its own address instead. What that leaves under test is
// the collection loop -- several answers, each taken with the address it came
// from -- and not the delivery, which needs a network.
static void
test_b_several_answers_are_told_apart(void)
{
    katherine_discovered_t found[4];
    size_t count = 0;
    kthread_t thread;

    if (mocks_init(2) != 2) {
        printf("# SKIP cannot bind %s:%d (a second loopback address may not be configured)\n",
            ADDRESS_B, READOUT_PORT);
        mocks_fini(2);
        return;
    }

    kthread_t late;

    KT_REQUIRE(kthread_start(&thread, mock_readout, &g_mocks[0]) == 0);

    // The second answer goes out a moment later, by when the discovery has
    // bound the reply port -- before that there is nothing listening and the
    // datagram is dropped.
    KT_REQUIRE(katherine_udp_set_remote(&g_mocks[1].udp, HOST_ADDRESS, READOUT_PORT) == 0);
    KT_REQUIRE(kthread_start(&late, mock_late_answer, &g_mocks[1]) == 0);

    count = 4;
    KT_CHECK_EQ(katherine_device_discover_udp(ADDRESS_A, REPLY_PORT, DISCOVER_MS, found, &count), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);
    KT_CHECK_EQ(kthread_join(&late), 0);

    printf("# collected %zu answer(s)\n", count);
    for (size_t i = 0; i < count; ++i) {
        printf("#   %s\n", found[i].address);
    }

    KT_REQUIRE(count == 2);

    // Either order: one answer was queued before the request went out and
    // the other arrives after it.
    const bool forward = strcmp(found[0].address, FOUND_A) == 0;
    KT_CHECK_STR_EQ(found[forward ? 0 : 1].address, FOUND_A);

    // The two readouts differ in the one thing a discovery can tell about a
    // firmware: the first answered on the port the request named, the second
    // ignored it and answered on the standard one.
    KT_CHECK(!found[forward ? 0 : 1].legacy);
    KT_CHECK(found[forward ? 1 : 0].legacy);

    KT_CHECK_STR_EQ(found[forward ? 1 : 0].address, FOUND_B);

    mocks_fini(2);
}

// c) Nothing listening is not an error: a discovery that finds no readout
// reports none, having no way to tell an empty subnet from a quiet one.
static void
test_c_finding_nothing_is_not_a_failure(void)
{
    katherine_discovered_t found[2];
    size_t count = 7; /* poisoned, so a call that forgets to set it fails */

    count = 2;
    KT_CHECK_EQ(katherine_device_discover_udp(ADDRESS_A, REPLY_PORT, 50, found, &count), 0);
    KT_CHECK_EQ(count, 0);
}

// d) And the arguments that cannot work are refused rather than sent: a
// reply port the firmware would ignore, no room for an answer, and a limit
// shorter than the parameter can express.
static void
test_d_impossible_requests_are_refused(void)
{
    katherine_discovered_t found[2];
    size_t count = 7;

    count = 2;
    KT_CHECK_EQ(katherine_device_discover_udp(ADDRESS_A, KATHERINE_DISCOVER_MIN_PORT - 1, DISCOVER_MS, found, &count),
        KATHERINE_E_INVAL);
    KT_CHECK_EQ(count, 0);

    // The standard port is the discovery's own legacy socket, so naming it
    // as the reply port would bind it twice.
    count = 2;
    KT_CHECK_EQ(katherine_device_discover_udp(ADDRESS_A, READOUT_PORT, DISCOVER_MS, found, &count),
        KATHERINE_E_INVAL);
    KT_CHECK_EQ(count, 0);

    // A capacity of zero is the one the in-out parameter cannot distinguish
    // from a result, so it is refused rather than returned as "none found".
    count = 0;
    KT_CHECK_EQ(katherine_device_discover_udp(ADDRESS_A, REPLY_PORT, DISCOVER_MS, found, &count),
        KATHERINE_E_INVAL);
    KT_CHECK_EQ(count, 0);

    // A zero timeout would leave the sessions with no receive timeout at
    // all, so the call would block instead of returning early.
    count = 2;
    KT_CHECK_EQ(katherine_device_discover_udp(ADDRESS_A, REPLY_PORT, 0, found, &count),
        KATHERINE_E_INVAL);
    KT_CHECK_EQ(count, 0);
}

// e) The discovery's own request, delivered back to it. Aimed at this host,
// the request reaches the standard-port socket the discovery itself just
// bound -- and a command and its response differ only in the fields a
// readout fills in, so nothing in the datagram marks it as a request. Were
// it not compared against the bytes that were sent, it would be reported as
// a readout, and a legacy one at that.
static void
test_e_the_request_coming_back_is_not_a_readout(void)
{
    katherine_discovered_t found[2];
    size_t count = 2;

    KT_CHECK_EQ(katherine_device_discover_udp(HOST_ADDRESS, REPLY_PORT, 50, found, &count), 0);
    KT_CHECK_EQ(count, 0);
}

// f) Nowhere to write the answers is refused before anything is written
// through either pointer.
static void
test_f_null_outputs_are_refused(void)
{
    katherine_discovered_t found[2];
    size_t count = 7;

    KT_CHECK_EQ(katherine_device_discover_udp(ADDRESS_A, REPLY_PORT, DISCOVER_MS, NULL, &count),
        KATHERINE_E_INVAL);
    KT_CHECK_EQ(count, 7);

    KT_CHECK_EQ(katherine_device_discover_udp(ADDRESS_A, REPLY_PORT, DISCOVER_MS, found, NULL),
        KATHERINE_E_INVAL);
}

// g) The address asked may name its link, and what comes back is the same
// readout under the same name -- which is what lets a caller feed a discovered
// address straight back to katherine_device_init().
static void
test_g_the_address_asked_may_name_its_link(void)
{
    katherine_discovered_t found[4];
    size_t count = 0;
    kthread_t thread;

    if (mocks_init(1) != 1) {
        printf("# SKIP cannot bind %s:%d\n", ADDRESS_A, READOUT_PORT);
        return;
    }

    KT_REQUIRE(kthread_start(&thread, mock_readout, &g_mocks[0]) == 0);

    count = 1;
    KT_CHECK_EQ(katherine_device_discover_udp(FOUND_A, REPLY_PORT, DISCOVER_MS, found, &count), 0);
    KT_CHECK_EQ(kthread_join(&thread), 0);

    KT_CHECK(g_mocks[0].got_request);
    KT_REQUIRE(count == 1);
    KT_CHECK_STR_EQ(found[0].address, FOUND_A);

    mocks_fini(1);
}

// h) An address this call cannot ask over. Refused before anything is bound,
// so these hold with nothing listening.
static void
test_h_an_address_for_another_link_is_refused(void)
{
    katherine_discovered_t found[4];
    size_t count;

    // A link that exists, asked of the wrong discovery.
    count = 1;
    KT_CHECK_EQ(katherine_device_discover_udp("usb://2-3.7.1", REPLY_PORT, DISCOVER_MS, found, &count), KATHERINE_E_ADDRESS);

    // A scheme no transport answers to.
    count = 1;
    KT_CHECK_EQ(katherine_device_discover_udp("http://127.0.0.2", REPLY_PORT, DISCOVER_MS, found, &count), KATHERINE_E_ADDRESS);

    // A scheme and no readout.
    count = 1;
    KT_CHECK_EQ(katherine_device_discover_udp("udp://", REPLY_PORT, DISCOVER_MS, found, &count), KATHERINE_E_ADDRESS);

    // No address at all, which used to reach inet_pton(3).
    count = 1;
    KT_CHECK_EQ(katherine_device_discover_udp(NULL, REPLY_PORT, DISCOVER_MS, found, &count), KATHERINE_E_ADDRESS);
}

int
main(void)
{
    KT_RUN(test_a_one_readout_is_recorded_under_its_address);
    KT_RUN(test_b_several_answers_are_told_apart);
    KT_RUN(test_c_finding_nothing_is_not_a_failure);
    KT_RUN(test_d_impossible_requests_are_refused);
    KT_RUN(test_e_the_request_coming_back_is_not_a_readout);
    KT_RUN(test_f_null_outputs_are_refused);
    KT_RUN(test_g_the_address_asked_may_name_its_link);
    KT_RUN(test_h_an_address_for_another_link_is_refused);
    return kt_summary();
}
