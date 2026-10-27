# Cython declarations for katherine/device.h.
# Created 3.2.19 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libc.stdint cimport uint8_t, uint16_t, uint32_t
from libcpp cimport bool

cimport ctransport

cdef extern from 'katherine/device.h':
    ctypedef struct katherine_discovered_t:
        char *address
        bint legacy
    cdef int KATHERINE_DISCOVER_MIN_PORT
    int katherine_device_discover_udp(const char *address, uint16_t reply_port, uint32_t timeout_ms, katherine_discovered_t *out, size_t *count)
    int katherine_device_discover_usb(katherine_discovered_t *out, size_t *count)

    ctypedef struct katherine_device_info_t:
        uint8_t hw_type
        uint8_t hw_revision
        uint16_t serial_number
        uint16_t fw_version
        uint8_t chip_count
        bool legacy

    ctypedef enum katherine_chip_type_t:
        KATHERINE_CHIP_UNKNOWN
        KATHERINE_CHIP_TPX2
        KATHERINE_CHIP_TPX3
        KATHERINE_CHIP_TPX4

    const char *katherine_str_chip_type(katherine_chip_type_t v)

    ctypedef enum katherine_device_flags_t:
        KATHERINE_DEVICE_DONT_ENUMERATE
        KATHERINE_DEVICE_DONT_CHANGE_UDP_PORTS
        KATHERINE_DEVICE_DONT_SELECT_INTERFACE

    ctypedef struct katherine_device_derived_info_t:
        const char *name
        katherine_chip_type_t chip_type
        uint8_t gen
        uint8_t max_chip_count
        uint8_t bias_supply_count
        float bias_min_volts
        float bias_max_volts
        uint8_t accessible_gpio_count
        uint8_t all_gpio_count
        uint8_t tdc_channel_count
        uint8_t pulse_channel_count
        bool supported

    ctypedef struct katherine_device_t:
        ctransport.katherine_transport_t control_transport
        katherine_device_info_t info
        katherine_device_derived_info_t derived_info
        void *acquisition

    int katherine_device_info_snprint(char *buf, size_t cap, const katherine_device_info_t *v)
    int katherine_device_derived_info_snprint(char *buf, size_t cap, const katherine_device_derived_info_t *v)
    katherine_device_derived_info_t katherine_device_derived_info_recognize(uint8_t hw_type)

    int katherine_device_snprint(char *buf, size_t cap, const katherine_device_t *v)

    int katherine_device_init(katherine_device_t *device, const char *address, uint32_t flags)
    void katherine_device_fini(katherine_device_t *device)
    int katherine_device_enumerate(katherine_device_t *device)
    int katherine_device_declare(katherine_device_t *device, const katherine_device_info_t *info)
    int katherine_device_select_interface(katherine_device_t *device)
    bool katherine_device_can_correct_timestamp_phase(const katherine_device_t *device)
