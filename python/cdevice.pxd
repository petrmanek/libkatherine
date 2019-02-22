# Katherine Control Library
#
# This file was created on 3.2.19 by Petr Manek.
# 
# Contents of this file are copyrighted and subject to license
# conditions specified in the LICENSE file located in the top
# directory.

cdef extern from 'katherine/device.h':
    ctypedef struct katherine_device_t:
        pass

    int katherine_device_init(katherine_device_t *device, const char *addr)
    void katherine_device_fini(katherine_device_t *device)
