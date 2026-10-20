/**
 * \file
 * \brief Naming one readout on a bus, and finding the ones that are there.
 *
 * Two readouts of the same model carry the same vendor and product
 * identifiers, so those cannot tell them apart. Where each sits can: the bus
 * and the chain of port numbers from the root hub down to it. This asserts
 * that the chain round-trips through the address form, that opening matches
 * on the whole of it rather than its last element, and that discovery hands
 * back an address which opens the device it names.
 *
 * Driven against the stand-in libusb, so it needs no bus.
 *
 * \author Petr Mánek
 * \date 19.10.26
 *
 * \copyright Copyright (c) 2018 Petr Mánek.
 * This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <katherine/katherine.h>

#include "transport/usb.h"

#include "fake_libusb/libusb.h"
#include "ktest.h"

// The readout's own identifiers, which the library holds as constants:
// a stand-in presenting anything else would never match.
#define VENDOR  0x16D0
#define PRODUCT 0x118A

/* Two readouts of the same model, each in port 1 of its own hub. Their last
 * port number is identical, which is exactly the case a leaf-only address
 * cannot distinguish. */
static const uint8_t PLACE_A[] = {3, 7, 1};
static const uint8_t PLACE_B[] = {4, 2, 1};

static katherine_usb_config_t
config_at(const char *address)
{
    katherine_usb_config_t config = katherine_usb_config_default();
    KT_CHECK_EQ(katherine_usb_config_set_address(&config, address), KATHERINE_E_OK);

    return config;
}

// ------------------------------------------------------------------

/* The chain survives the address form in both directions, which is what lets
 * a discovered address be handed straight back. */
static void
test_an_address_round_trips(void)
{
    katherine_usb_config_t config = katherine_usb_config_default();

    KT_CHECK_EQ(katherine_usb_config_set_address(&config, "usb://2-3.7.1.1"), KATHERINE_E_OK);
    KT_CHECK_EQ(config.bus, 2);
    KT_REQUIRE(config.port_count == 4);
    KT_CHECK_EQ(config.ports[0], 3);
    KT_CHECK_EQ(config.ports[1], 7);
    KT_CHECK_EQ(config.ports[2], 1);
    KT_CHECK_EQ(config.ports[3], 1);

    char buf[KATHERINE_DEVICE_ADDRESS_STR_SIZE];
    KT_CHECK(katherine_usb_config_snprint_address(buf, sizeof(buf), &config) > 0);
    KT_CHECK_STR_EQ(buf, "usb://2-3.7.1.1");

    /* A device straight on a root hub has one port and no dots. */
    KT_CHECK_EQ(katherine_usb_config_set_address(&config, "usb://1-4"), KATHERINE_E_OK);
    KT_CHECK_EQ(config.port_count, 1);
    KT_CHECK(katherine_usb_config_snprint_address(buf, sizeof(buf), &config) > 0);
    KT_CHECK_STR_EQ(buf, "usb://1-4");
}

/* The widest address a bus can produce still fits the buffer the library
 * sizes for it, which is the arithmetic KATHERINE_DEVICE_ADDRESS_STR_SIZE
 * was chosen from. */
static void
test_the_deepest_address_fits(void)
{
    katherine_usb_config_t config = katherine_usb_config_default();
    const char *deepest           = "usb://255-255.255.255.255.255.255.255";

    KT_CHECK_EQ(katherine_usb_config_set_address(&config, deepest), KATHERINE_E_OK);
    KT_CHECK_EQ(config.port_count, KATHERINE_USB_PORT_DEPTH);

    char buf[KATHERINE_DEVICE_ADDRESS_STR_SIZE];
    const int written = katherine_usb_config_snprint_address(buf, sizeof(buf), &config);
    KT_CHECK(written > 0 && (size_t) written < sizeof(buf));
    KT_CHECK_STR_EQ(buf, deepest);
}

/* What is not an address. A tree deeper than the specification allows is
 * among them: nothing can be at such a place, so a string naming one is a
 * mistake rather than a device that is missing. */
static void
test_a_malformed_address_is_refused(void)
{
    katherine_usb_config_t config = katherine_usb_config_default();

    KT_CHECK_EQ(katherine_usb_config_set_address(&config, "udp://192.168.1.42"), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_usb_config_set_address(&config, "usb://2"), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_usb_config_set_address(&config, "usb://2-"), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_usb_config_set_address(&config, "usb://2-3."), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_usb_config_set_address(&config, "usb://2-3.0"), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_usb_config_set_address(&config, "usb://2-999"), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_usb_config_set_address(&config, "usb://2-1.1.1.1.1.1.1.1"), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_usb_config_set_address(&config, "usb://"), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_usb_config_set_address(&config, NULL), KATHERINE_E_ADDRESS);
    KT_CHECK_EQ(katherine_usb_config_set_address(NULL, "usb://2-1"), KATHERINE_E_ADDRESS);
}

/* The whole chain decides, not its last element. Both readouts end in port 1,
 * so an address that kept only that would open whichever came first in the
 * device list -- which is the defect this addressing exists to remove. */
static void
test_the_whole_chain_decides(void)
{
    fake_usb_reset();
    fake_usb_clear_devices();
    fake_usb_attach(VENDOR, PRODUCT, 1, PLACE_A, 3);
    fake_usb_attach(VENDOR, PRODUCT, 1, PLACE_B, 3);

    katherine_transport_t t;

    const katherine_usb_config_t a = config_at("usb://1-3.7.1");
    KT_CHECK_EQ(katherine_transport_init_usb(&t, &a), KATHERINE_E_OK);
    katherine_transport_fini(&t);

    const katherine_usb_config_t b = config_at("usb://1-4.2.1");
    KT_CHECK_EQ(katherine_transport_init_usb(&t, &b), KATHERINE_E_OK);
    katherine_transport_fini(&t);

    /* A place nothing is at, though its leaf port matches both of them. */
    const katherine_usb_config_t nowhere = config_at("usb://1-9.9.1");
    KT_CHECK_EQ(katherine_transport_init_usb(&t, &nowhere), KATHERINE_E_ADDRESS);

    /* The right chain on the wrong bus is the wrong device. */
    const katherine_usb_config_t other_bus = config_at("usb://2-3.7.1");
    KT_CHECK_EQ(katherine_transport_init_usb(&t, &other_bus), KATHERINE_E_ADDRESS);
}

/* A configuration naming no place still opens the one readout attached,
 * which is what a caller with a single device should not have to think
 * about. */
static void
test_no_place_takes_whatever_is_there(void)
{
    fake_usb_reset();
    fake_usb_clear_devices();
    fake_usb_attach(VENDOR, PRODUCT, 1, PLACE_A, 3);

    katherine_usb_config_t config = katherine_usb_config_default();
    KT_CHECK_EQ(config.port_count, 0);

    katherine_transport_t t;
    KT_CHECK_EQ(katherine_transport_init_usb(&t, &config), KATHERINE_E_OK);
    katherine_transport_fini(&t);
}

/* Discovery reports every readout carrying the identifiers, under an address
 * that opens it. The round trip is the property worth having: a caller never
 * has to assemble one of these by hand. */
static void
test_discovery_reports_openable_addresses(void)
{
    fake_usb_reset();
    fake_usb_clear_devices();
    fake_usb_attach(VENDOR, PRODUCT, 1, PLACE_A, 3);
    fake_usb_attach(VENDOR, PRODUCT, 2, PLACE_B, 3);
    /* Something else on the same bus, which must not be reported. */
    fake_usb_attach(0x0000, 0x9999, 1, PLACE_B, 3);

    katherine_discovered_t found[4];
    size_t count = 4;
    KT_CHECK_EQ(katherine_device_discover_usb(found, &count), KATHERINE_E_OK);
    KT_REQUIRE(count == 2);

    KT_CHECK_STR_EQ(found[0].address, "usb://1-3.7.1");
    KT_CHECK_STR_EQ(found[1].address, "usb://2-4.2.1");

    /* A bus has no reply-port behaviour, so the flag the UDP side carries is
     * false rather than unset-and-meaningful. */
    KT_CHECK(!found[0].legacy);

    for (size_t i = 0; i < count; ++i) {
        katherine_usb_config_t config = katherine_usb_config_default();
        KT_CHECK_EQ(katherine_usb_config_set_address(&config, found[i].address), KATHERINE_E_OK);

        katherine_transport_t t;
        KT_CHECK_EQ(katherine_transport_init_usb(&t, &config), KATHERINE_E_OK);
        katherine_transport_fini(&t);
    }
}

/* Nothing attached is not an error, exactly as on the UDP side. */
static void
test_an_empty_bus_is_not_a_failure(void)
{
    fake_usb_reset();
    fake_usb_set_present(0);

    katherine_discovered_t found[2];
    size_t count = 2;
    KT_CHECK_EQ(katherine_device_discover_usb(found, &count), KATHERINE_E_OK);
    KT_CHECK_EQ(count, 0u);

    fake_usb_set_present(1);
}

/* The argument checks, which happen before the bus is touched. */
static void
test_discovery_refuses_impossible_requests(void)
{
    katherine_discovered_t found[2];
    size_t count = 2;

    KT_CHECK_EQ(katherine_device_discover_usb(NULL, &count), KATHERINE_E_INVAL);
    KT_CHECK_EQ(katherine_device_discover_usb(found, NULL), KATHERINE_E_INVAL);

    count = 0;
    KT_CHECK_EQ(katherine_device_discover_usb(found, &count), KATHERINE_E_INVAL);
}

int
main(void)
{
    KT_RUN(test_an_address_round_trips);
    KT_RUN(test_the_deepest_address_fits);
    KT_RUN(test_a_malformed_address_is_refused);
    KT_RUN(test_the_whole_chain_decides);
    KT_RUN(test_no_place_takes_whatever_is_there);
    KT_RUN(test_discovery_reports_openable_addresses);
    KT_RUN(test_an_empty_bus_is_not_a_failure);
    KT_RUN(test_discovery_refuses_impossible_requests);
    return kt_summary();
}
