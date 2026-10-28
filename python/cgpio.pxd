# Cython declarations for katherine/gpio.h.
# Created 18.9.26 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libcpp cimport bool
from libc.stdint cimport uint32_t
from cdevice cimport katherine_device_t
from cconfig cimport katherine_trigger_edge_t

cdef extern from 'katherine/gpio.h':
    cdef int KATHERINE_MAX_GPIO_COUNT
    cdef int KATHERINE_GPIO_TICK_NS

    ctypedef enum katherine_gpio_mapping_t:
        KATHERINE_GPIO_INPUT
        KATHERINE_GPIO_LOW
        KATHERINE_GPIO_PULSE_INTERNAL
        KATHERINE_GPIO_PULSE_OUT_1
        KATHERINE_GPIO_PULSE_OUT_2
        KATHERINE_GPIO_PULSE_OUT_3
        KATHERINE_GPIO_LEVEL_0
        KATHERINE_GPIO_LEVEL_1
        KATHERINE_GPIO_LEVEL_2
        KATHERINE_GPIO_LEVEL_3
        KATHERINE_GPIO_LEVEL_4
        KATHERINE_GPIO_LEVEL_5
        KATHERINE_GPIO_LEVEL_6
        KATHERINE_GPIO_LEVEL_7
        KATHERINE_GPIO_READY_LOCAL
        KATHERINE_GPIO_READY_COMPOSED
        KATHERINE_GPIO_EVENT_TRIGGER
        KATHERINE_GPIO_CHIP_0_PIXEL_DATA
        KATHERINE_GPIO_CHIP_1_PIXEL_DATA
        KATHERINE_GPIO_CHIP_2_PIXEL_DATA
        KATHERINE_GPIO_CHIP_3_PIXEL_DATA
        KATHERINE_GPIO_SHUTTER
        KATHERINE_GPIO_CASCADE_CHAIN
        KATHERINE_GPIO_CLOCK
        KATHERINE_GPIO_SYNC_SIGNAL
        KATHERINE_GPIO_SPARE_0
        KATHERINE_GPIO_SPARE_1
        KATHERINE_GPIO_SPARE_2
        KATHERINE_GPIO_SPARE_3
        KATHERINE_GPIO_SPARE_4
        KATHERINE_GPIO_SPARE_5
        KATHERINE_GPIO_SPARE_6
        KATHERINE_GPIO_HIGH

    ctypedef enum katherine_gpio_ready_t:
        KATHERINE_GPIO_READY_UNUSED
        KATHERINE_GPIO_READY_WHEN_HIGH
        KATHERINE_GPIO_READY_WHEN_LOW

    ctypedef enum katherine_gpio_monitor_signal_t:
        KATHERINE_GPIO_MONITOR_SIGNAL_UNKNOWN
        KATHERINE_GPIO_MONITOR_SIGNAL_CONSTANT
        KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC

    ctypedef struct katherine_gpio_channel_config_t:
        katherine_gpio_mapping_t mapping
        bool termination
        katherine_gpio_ready_t ready

    ctypedef struct katherine_gpio_config_t:
        katherine_gpio_channel_config_t *channel

    ctypedef struct katherine_gpio_channel_monitor_t:
        bool level
        katherine_gpio_monitor_signal_t signal
        uint32_t high_ticks
        uint32_t low_ticks

    ctypedef struct katherine_gpio_monitor_t:
        katherine_gpio_channel_monitor_t *channel

    const char *katherine_str_gpio_mapping(katherine_gpio_mapping_t mapping)
    const char *katherine_str_gpio_monitor_signal(katherine_gpio_monitor_signal_t signal)

    double katherine_gpio_channel_frequency(const katherine_gpio_channel_monitor_t *v)
    double katherine_gpio_channel_duty_cycle(const katherine_gpio_channel_monitor_t *v)

    int katherine_gpio_channel_monitor_snprint(char *buf, size_t cap, const katherine_gpio_channel_monitor_t *v)
    int katherine_gpio_monitor_snprint(char *buf, size_t cap, const katherine_gpio_monitor_t *v)

    int katherine_set_gpio_config(katherine_device_t *device, const katherine_gpio_config_t *config)
    int katherine_set_composed_ready_signal(katherine_device_t *device, const katherine_gpio_config_t *config)
    int katherine_probe_gpio(katherine_device_t *device, katherine_gpio_monitor_t *state)
