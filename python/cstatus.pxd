# Cython declarations for katherine/status.h.
# Created 3.2.19 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from cdevice cimport katherine_device_t

cdef extern from 'katherine/status.h':
    ctypedef struct katherine_readout_status_t:
        pass
    ctypedef struct katherine_comm_status_t:
        pass
    cdef int KATHERINE_CHIP_ID_STR_SIZE

    int katherine_get_readout_status(katherine_device_t *device, katherine_readout_status_t *status)
    int katherine_get_comm_status(katherine_device_t *device, katherine_comm_status_t *status)
    int katherine_get_chip_id(katherine_device_t *device, char *s_chip_id)
    int katherine_get_readout_temperature(katherine_device_t *device, float *temperature)
    int katherine_get_sensor_temperature(katherine_device_t *device, float *temperature)
    int katherine_perform_digital_test(katherine_device_t *device)
    int katherine_get_adc_voltage(katherine_device_t *device, unsigned char channel_id, float *voltage)
