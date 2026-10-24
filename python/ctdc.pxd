# Cython declarations for katherine/tdc.h.
# Created 18.9.26 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from libcpp cimport bool
from libc.stdint cimport uint32_t
from cconfig cimport katherine_trigger_edge_t
from cdevice cimport katherine_device_t

cdef extern from 'katherine/tdc.h':
    cdef enum: KATHERINE_MAX_TDC_CHANNEL_COUNT

    ctypedef enum katherine_tdc_source_t:
        KATHERINE_TDC_SOURCE_GPIO_0
        KATHERINE_TDC_SOURCE_GPIO_1
        KATHERINE_TDC_SOURCE_GPIO_2
        KATHERINE_TDC_SOURCE_GPIO_3
        KATHERINE_TDC_SOURCE_CHIP_0_FEEDBACK
        KATHERINE_TDC_SOURCE_CHIP_1_FEEDBACK
        KATHERINE_TDC_SOURCE_CHIP_2_FEEDBACK
        KATHERINE_TDC_SOURCE_CHIP_3_FEEDBACK

    ctypedef struct katherine_tdc_channel_config_t:
        bool enabled
        katherine_tdc_source_t source
        katherine_trigger_edge_t edge

    ctypedef struct katherine_tdc_config_t:
        katherine_tdc_channel_config_t *channel

    const char *katherine_str_tdc_source(katherine_tdc_source_t source)

    int katherine_set_tdc_config(katherine_device_t *device, const katherine_tdc_config_t *config)
    ctypedef struct katherine_tdc_counts_t:
        uint32_t channel[8]  # KATHERINE_MAX_TDC_CHANNEL_COUNT

    int katherine_get_tdc_counts(katherine_device_t *device, katherine_tdc_counts_t *counts)
