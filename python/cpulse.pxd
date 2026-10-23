# Cython declarations for katherine/pulse.h.
# Created 18.9.26 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libcpp cimport bool
from libc.stdint cimport uint32_t
from cdevice cimport katherine_device_t

cdef extern from 'katherine/pulse.h':
    cdef int KATHERINE_MAX_PULSE_CHANNEL_COUNT

    ctypedef enum katherine_pulse_output_t:
        KATHERINE_PULSE_OUTPUT_PULSE
        KATHERINE_PULSE_OUTPUT_PULSE_INVERTED
        KATHERINE_PULSE_OUTPUT_PASSTHROUGH
        KATHERINE_PULSE_OUTPUT_PASSTHROUGH_INVERTED

    ctypedef enum katherine_pulse_source_t:
        KATHERINE_PULSE_SOURCE_RISING_EDGE
        KATHERINE_PULSE_SOURCE_FALLING_EDGE
        KATHERINE_PULSE_SOURCE_OSCILLATOR

    ctypedef struct katherine_pulse_channel_config_t:
        bool enabled
        katherine_pulse_output_t output
        double length_seconds
        double delay_seconds

    ctypedef struct katherine_pulse_config_t:
        katherine_pulse_channel_config_t *channel
        bool only_when_ready
        katherine_pulse_source_t source
        double oscillator_frequency_hz

    ctypedef struct katherine_pulse_counts_t:
        double low_seconds
        double high_seconds
        uint32_t rising_edges
        uint32_t clock_mhz

    const char *katherine_str_pulse_output(katherine_pulse_output_t output)
    const char *katherine_str_pulse_source(katherine_pulse_source_t source)

    int katherine_set_pulse_generator(katherine_device_t *device, const katherine_pulse_config_t *config)
    int katherine_get_pulse_generator_counts(katherine_device_t *device, katherine_pulse_counts_t *counts)
    int katherine_reset_pulse_generator_counts(katherine_device_t *device)
