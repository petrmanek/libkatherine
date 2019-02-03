# cdevice.pxd

cdef extern from '../c/include/katherine/device.h':
    ctypedef struct katherine_device_t:
        pass

    int katherine_device_init(katherine_device_t *device, const char *addr)
    void katherine_device_fini(katherine_device_t *device)
