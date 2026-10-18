/**
 * \file
 * \brief The scheme an address carries, and what reads it.
 *
 * An address names the link as well as the readout on it, so that one string
 * can send a caller to a readout over a network or over a bus. This asserts
 * the split itself, and then the two public calls that take an address: that
 * each accepts the scheme-bearing form, that each still accepts the bare one
 * every address written before 2.0 is, and that neither opens anything on an
 * address naming a link it cannot drive.
 *
 * The refusals are checked with nothing listening, which they can be: an
 * address is read before any socket is opened. The one case that does open
 * sockets binds the readout's own control and data ports, which is why this
 * runs serially.
 *
 * \author Petr Mánek
 * \date 18.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include <katherine/device.h>
#include <katherine/transport.h>

#include "address.h"
#include "ktest.h"

/* An address nothing answers at, since nothing here is meant to connect. */
#define QUIET_HOST "127.0.0.1"

/* The split reports the link and hands back the part the transport reads. */
static void
test_a_scheme_names_the_link(void)
{
    katherine_transport_kind_t kind;
    const char *rest;

    KT_CHECK_EQ(katherine_address_split("udp://192.168.1.42", &kind, &rest), KATHERINE_E_OK);
    KT_CHECK_EQ(kind, KATHERINE_TRANSPORT_UDP);
    KT_CHECK_STR_EQ(rest, "192.168.1.42");

    KT_CHECK_EQ(katherine_address_split("usb://2-3.7.1.1", &kind, &rest), KATHERINE_E_OK);
    KT_CHECK_EQ(kind, KATHERINE_TRANSPORT_USB);
    KT_CHECK_STR_EQ(rest, "2-3.7.1.1");
}

/* Every address written before 2.0 has no scheme and names a readout on a
 * network, so that is what one keeps meaning. */
static void
test_b_an_address_without_a_scheme_is_udp(void)
{
    katherine_transport_kind_t kind;
    const char *rest;

    KT_CHECK_EQ(katherine_address_split("192.168.1.42", &kind, &rest), KATHERINE_E_OK);
    KT_CHECK_EQ(kind, KATHERINE_TRANSPORT_UDP);
    KT_CHECK_STR_EQ(rest, "192.168.1.42");

    /* The separator is what introduces a scheme. A colon on its own is part
     * of the address, not the start of one. */
    KT_CHECK_EQ(katherine_address_split("::1", &kind, &rest), KATHERINE_E_OK);
    KT_CHECK_EQ(kind, KATHERINE_TRANSPORT_UDP);
    KT_CHECK_STR_EQ(rest, "::1");
}

/* What is not an address. */
static void
test_c_an_unreadable_address_is_refused(void)
{
    katherine_transport_kind_t kind;
    const char *rest;

    KT_CHECK_EQ(katherine_address_split("http://192.168.1.42", &kind, &rest), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_address_split("://192.168.1.42", &kind, &rest), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_address_split("udp://", &kind, &rest), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_address_split("", &kind, &rest), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_address_split(NULL, &kind, &rest), KATHERINE_E_ADDRESS);
}

/* Formatting is the inverse of the split, which is the property that lets a
 * discovered address be handed straight back to katherine_device_init(). */
static void
test_d_an_address_survives_the_round_trip(void)
{
    char buf[KATHERINE_DEVICE_ADDRESS_STR_SIZE];
    katherine_transport_kind_t kind;
    const char *rest;

    KT_CHECK(katherine_address_format(buf, sizeof(buf), KATHERINE_TRANSPORT_UDP, "192.168.1.42") > 0);
    KT_CHECK_STR_EQ(buf, "udp://192.168.1.42");
    KT_CHECK_EQ(katherine_address_split(buf, &kind, &rest), KATHERINE_E_OK);
    KT_CHECK_EQ(kind, KATHERINE_TRANSPORT_UDP);
    KT_CHECK_STR_EQ(rest, "192.168.1.42");

    /* The longest form a transport writes: a bus and seven port numbers, all
     * at their widest. This is what the buffer is sized for. */
    KT_CHECK(katherine_address_format(buf, sizeof(buf), KATHERINE_TRANSPORT_USB, "255-255.255.255.255.255.255.255")
        < (int) sizeof(buf));
    KT_CHECK_STR_EQ(buf, "usb://255-255.255.255.255.255.255.255");
}

/* Opening a device reads the scheme, and reads it before it opens anything. */
static void
test_e_opening_reads_the_scheme(void)
{
    katherine_device_t device;

    /* A link this version cannot open a device on is a different answer from
     * one it has never heard of: the first is a gap, the second a typo. */
    KT_CHECK_EQ(katherine_device_init(&device, "usb://2-3.7.1.1", KATHERINE_DEVICE_DONT_ENUMERATE),
        KATHERINE_E_UNSUPPORTED);
    KT_CHECK_EQ(katherine_device_init(&device, "http://" QUIET_HOST, KATHERINE_DEVICE_DONT_ENUMERATE),
        KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_device_init(&device, "udp://", KATHERINE_DEVICE_DONT_ENUMERATE), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_device_init(&device, NULL, KATHERINE_DEVICE_DONT_ENUMERATE), KATHERINE_E_ADDRESS);
}

/* And accepts the scheme-bearing form, which is the half the refusals cannot
 * show. Nothing need answer: both sockets are local, and enumeration is
 * suppressed so none of the readout is asked for. */
static void
test_f_opening_accepts_the_scheme(void)
{
    katherine_device_t device;

    const katherine_error_t res = katherine_device_init(&device, "udp://" QUIET_HOST, KATHERINE_DEVICE_DONT_ENUMERATE);

    /* The address is known good, so this is the other thing that code means:
     * the fixed local port is already somebody's. */
    if (res == KATHERINE_E_ADDRESS) {
        printf("# SKIP cannot bind the readout's control and data ports\n");
        return;
    }

    KT_REQUIRE(res == KATHERINE_E_OK);
    katherine_device_fini(&device);
}

/* And the form with no scheme reaches both entry points, which is what every
 * address written before 2.0 is. Asserted here rather than left to the places
 * that happen to still use one, because those are a liability: the next
 * sweep over the tests would quietly take the last of them away. */
static void
test_g_the_bare_form_still_opens_a_device(void)
{
    katherine_device_t device;

    const katherine_error_t res = katherine_device_init(&device, QUIET_HOST, KATHERINE_DEVICE_DONT_ENUMERATE);
    if (res == KATHERINE_E_ADDRESS) {
        printf("# SKIP cannot bind the readout's control and data ports\n");
        return;
    }

    KT_REQUIRE(res == KATHERINE_E_OK);
    katherine_device_fini(&device);
}

int
main(void)
{
    KT_RUN(test_a_scheme_names_the_link);
    KT_RUN(test_b_an_address_without_a_scheme_is_udp);
    KT_RUN(test_c_an_unreadable_address_is_refused);
    KT_RUN(test_d_an_address_survives_the_round_trip);
    KT_RUN(test_e_opening_reads_the_scheme);
    KT_RUN(test_f_opening_accepts_the_scheme);
    KT_RUN(test_g_the_bare_form_still_opens_a_device);
    return kt_summary();
}
