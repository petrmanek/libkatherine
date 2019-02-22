# Katherine Control Library
#
# This file was created on 3.2.19 by Petr Manek.
# 
# Contents of this file are copyrighted and subject to license
# conditions specified in the LICENSE file located in the top
# directory.

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
