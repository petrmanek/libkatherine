# cython: language_level=3

from cpython.mem cimport PyMem_Malloc, PyMem_Free
from os import strerror
import array

cimport cdevice
cimport cstatus


def check_return_code(int res):
    if res == 0:
        return

    raise OSError(res, strerror(res))


cdef class Device:
    cdef cdevice.katherine_device_t* _c_device
    def __cinit__(self, addr):
        self._c_device = <cdevice.katherine_device_t*> PyMem_Malloc(sizeof(cdevice.katherine_device_t))
        if self._c_device is NULL:
            raise MemoryError()

        res = cdevice.katherine_device_init(self._c_device, addr.encode())
        check_return_code(res)


    def __dealloc__(self):
        if self._c_device is not NULL:
            cdevice.katherine_device_fini(self._c_device)

        PyMem_Free(self._c_device)

    # TODO int katherine_get_readout_status(katherine_device_t *, katherine_readout_status_t *);

    # TODO int katherine_get_comm_status(katherine_device_t *, katherine_comm_status_t *);

    def get_chip_id(self):
        cdef char[:] chip_id = array.array('b', cstatus.KATHERINE_CHIP_ID_STR_SIZE)
        res = cstatus.katherine_get_chip_id(self._c_device, &chip_id[0])
        check_return_code(res)
        return <bytes> chip_id

    def get_readout_temperature(self):
        cdef float temp
        res = cstatus.katherine_get_readout_temperature(self._c_device, &temp)
        check_return_code(res)
        return temp

    def get_sensor_temperature(self):
        cdef float temp
        res = cstatus.katherine_get_sensor_temperature(self._c_device, &temp)
        check_return_code(res)
        return temp

    def perform_digital_test(self):
        res = cstatus.katherine_perform_digital_test(self._c_device)
        check_return_code(res)

    def get_adc_voltage(self, unsigned char channel_id):
        cdef float voltage
        res = cstatus.katherine_get_adc_voltage(self._c_device, channel_id, &voltage)
        check_return_code(res)
        return voltage
