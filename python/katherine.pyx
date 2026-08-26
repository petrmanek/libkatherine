# cython: language_level=3

# Cython module wrapping libkatherine for Python.
# Created 3.2.19 by Petr Mánek.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

from cpython.mem cimport PyMem_Malloc, PyMem_Free
from cpython.bytes cimport PyBytes_FromStringAndSize
from libc.stdint cimport uint8_t, uint16_t, uint32_t, int32_t
from libc.string cimport memcpy
from libcpp cimport bool
from enum import Enum, unique
import array

cimport cdevice
cimport cstatus
cimport cconfig
cimport cpx
cimport cpx_config
cimport cacquisition
cimport cudp
cimport cversion
cimport cerror


def check_return_code(int res):
    if res == 0:
        return

    message = cerror.katherine_strerror(res).decode('UTF-8')

    if res == -cerror.KATHERINE_E_TIMEOUT:
        raise TimeoutError(message)
    elif res == -cerror.KATHERINE_E_INVAL:
        raise ValueError(message)
    elif res == -cerror.KATHERINE_E_NOMEM:
        raise MemoryError()
    else:
        raise RuntimeError(message)


# Function pointer type shared by every katherine_*_snprint() declared in
# the cimported c*.pxd files: same (buf, cap, const T *) shape, T erased to
# void* here so that one helper (below) can call any of them. Casting a
# katherine_*_snprint reference to this type at each call site is safe: the
# calling convention does not depend on what T is, only C's own pointer
# arithmetic does, and that never happens on the caller's side of the cast.
ctypedef int (*snprint_fn_t)(char *, size_t, const void *)


cdef str _snprint_repr(snprint_fn_t fn, const void *v):
    """The two-call pattern every __repr__ below is a one-liner over: size
    with a NULL/0 call exactly like snprintf(), allocate that many bytes
    plus a NUL, then fill and decode. The single source of truth for the
    rendering itself is fn (one of the katherine_*_snprint() family in
    c/src/repr.c); nothing here reimplements any formatting."""
    cdef int n = fn(NULL, 0, v)
    if n < 0:
        return ''

    cdef bytearray buf = bytearray(n + 1)
    cdef char[:] view = buf
    fn(<char *> &view[0], n + 1, v)
    return bytes(view[:n]).decode('UTF-8')


# Version of the loaded library, e.g. '1.0.1'. Queried at import time from
# katherine_version_string(), so it always names the library actually linked
# in, not merely the one the bindings were compiled against.
__version__ = cversion.katherine_version_string().decode('UTF-8')


def version():
    return cversion.katherine_version()


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

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cdevice.katherine_device_snprint, self._c_device)

    def get_readout_status(self):
         cdef ReadoutStatus status = ReadoutStatus()
         res = cstatus.katherine_get_readout_status(self._c_device, &status._c_status)
         check_return_code(res)
         return status

    def get_comm_status(self):
         cdef CommStatus status = CommStatus()
         res = cstatus.katherine_get_comm_status(self._c_device, &status._c_status)
         check_return_code(res)
         return status

    def get_chip_id(self):
         cdef char[:] chip_id = array.array('b', [0] * cstatus.KATHERINE_CHIP_ID_STR_SIZE)
         cdef char *c_chip_id = &chip_id[0]
         res = cstatus.katherine_get_chip_id(self._c_device, c_chip_id)
         check_return_code(res)
         
         if chip_id[0] == 0:
            return None

         return c_chip_id.decode('UTF-8')

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

    def set_test_pulses(self, TestPulseConfig test_pulse_config):
         res = cconfig.katherine_set_test_pulses(self._c_device, &test_pulse_config._c_test_pulse_config)
         check_return_code(res)

    def configure(self, Config config):
         res = cconfig.katherine_configure(self._c_device, &config._c_config)
         check_return_code(res)

    def set_all_pixel_config(self, PxConfig px_config):
         res = cconfig.katherine_set_all_pixel_config(self._c_device, &px_config._c_px_config)
         check_return_code(res)

    def set_acq_time(self, double ns):
         res = cconfig.katherine_set_acq_time(self._c_device, ns)
         check_return_code(res)

    def set_acq_mode(self, acq_mode, bool fast_vco_enabled):
         res = cconfig.katherine_set_acq_mode(self._c_device, acq_mode.value, fast_vco_enabled)
         check_return_code(res)

    def set_no_frames(self, int no_frames):
         res = cconfig.katherine_set_no_frames(self._c_device, no_frames)
         check_return_code(res)

    def set_bias(self, unsigned char bias_id, float bias_value):
         res = cconfig.katherine_set_bias(self._c_device, bias_id, bias_value)
         check_return_code(res)

    def set_seq_readout_start(self, int arg):
         res = cconfig.katherine_set_seq_readout_start(self._c_device, arg)
         check_return_code(res)

    def acquisition_setup(self, Trigger start_trigger, bool delayed_start, Trigger end_trigger):
         res = cconfig.katherine_acquisition_setup(self._c_device, &start_trigger._c_trigger, delayed_start, &end_trigger._c_trigger)
         check_return_code(res)

    def set_sensor_register(self, reg_idx, int32_t reg_value):
         res = cconfig.katherine_set_sensor_register(self._c_device, reg_idx.value, reg_value)
         check_return_code(res)

    def update_sensor_registers(self):
         res = cconfig.katherine_update_sensor_registers(self._c_device)
         check_return_code(res)

    def output_block_config_update(self):
         res = cconfig.katherine_output_block_config_update(self._c_device)
         check_return_code(res)

    def timer_set(self):
         res = cconfig.katherine_timer_set(self._c_device)
         check_return_code(res)

    def set_dacs(self, Dacs dacs):
         res = cconfig.katherine_set_dacs(self._c_device, &dacs._c_dacs)
         check_return_code(res)


cdef class Udp:
    cdef cudp.katherine_udp_t* _c_udp

    def __cinit__(self, local_addr, uint16_t local_port, remote_addr, uint16_t remote_port, uint32_t timeout_ms):
         self._c_udp = <cudp.katherine_udp_t*> PyMem_Malloc(sizeof(cudp.katherine_udp_t))
         if self._c_udp is NULL:
             raise MemoryError()

         cdef char *c_local_addr = NULL
         cdef bytes local_addr_b
         if local_addr is not None:
             local_addr_b = local_addr.encode()
             c_local_addr = local_addr_b

         res = cudp.katherine_udp_init_bound(self._c_udp, c_local_addr, local_port, remote_addr.encode(), remote_port, timeout_ms)
         check_return_code(res)

    def __dealloc__(self):
         if self._c_udp is not NULL:
             cudp.katherine_udp_fini(self._c_udp)

         PyMem_Free(self._c_udp)

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cudp.katherine_udp_snprint, self._c_udp)

    # katherine_udp_mutex_lock()/katherine_udp_mutex_unlock() are deliberately
    # not wrapped here: they exist so that a caller with several C calls in a
    # row (e.g. Device pairing a command with its response) can hold the
    # session's mutex across all of them, which has no counterpart in a
    # binding that only ever exposes one call at a time.

    def send_exact(self, bytes data):
         cdef char *buf = data
         res = cudp.katherine_udp_send_exact(self._c_udp, buf, len(data))
         check_return_code(res)

    def recv_exact(self, size_t size):
         cdef bytes data = PyBytes_FromStringAndSize(NULL, size)
         cdef char *buf = data
         res = cudp.katherine_udp_recv_exact(self._c_udp, buf, size)
         check_return_code(res)
         return data

    def recv(self, size_t max_size):
         cdef bytes data = PyBytes_FromStringAndSize(NULL, max_size)
         cdef char *buf = data
         cdef size_t count = max_size
         res = cudp.katherine_udp_recv(self._c_udp, buf, &count)
         check_return_code(res)
         return data[:count]

    def set_remote(self, remote_addr, uint16_t remote_port):
         res = cudp.katherine_udp_set_remote(self._c_udp, remote_addr.encode(), remote_port)
         check_return_code(res)

    def pin_remote(self):
         cudp.katherine_udp_pin_remote(self._c_udp)


cdef class ReadoutStatus:
    cdef cstatus.katherine_readout_status_t _c_status

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cstatus.katherine_readout_status_snprint, &self._c_status)

    @property
    def hw_type(self):
       return self._c_status.hw_type

    @property
    def hw_revision(self):
       return self._c_status.hw_revision

    @property
    def hw_serial_number(self):
       return self._c_status.hw_serial_number

    @property
    def fw_version(self):
       return self._c_status.fw_version


cdef class CommStatus:
    cdef cstatus.katherine_comm_status_t _c_status

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cstatus.katherine_comm_status_snprint, &self._c_status)

    @property
    def comm_lines_mask(self):
       return self._c_status.comm_lines_mask

    @property
    def data_rate(self):
       return self._c_status.data_rate

    @property
    def chip_detected(self):
       return self._c_status.chip_detected


cdef class Trigger:
    cdef cconfig.katherine_trigger_t _c_trigger

    def __init__(self, enabled=False, channel=0, use_falling_edge=False, cdata=None):
         if cdata is None:
             self._c_trigger.enabled = enabled
             self._c_trigger.channel = channel
             self._c_trigger.use_falling_edge = use_falling_edge
         else:
             self._c_trigger = cdata

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cconfig.katherine_trigger_snprint, &self._c_trigger)

    @property
    def enabled(self):
       return self._c_trigger.enabled

    @enabled.setter
    def enabled(self, val):
       self._c_trigger.enabled = val

    @property
    def channel(self):
         return self._c_trigger.channel

    @channel.setter
    def channel(self, val):
         self._c_trigger.channel = val

    @property
    def use_falling_edge(self):
         return self._c_trigger.use_falling_edge

    @use_falling_edge.setter
    def use_falling_edge(self, val):
         self._c_trigger.use_falling_edge = val


cdef class TestPulseConfig:
    cdef cconfig.katherine_test_pulse_config_t _c_test_pulse_config

    def __init__(self, enabled=False, digital_only=False, external=False, count=0, period=0, phase=0, cdata=None):
         if cdata is None:
             self._c_test_pulse_config.enabled = enabled
             self._c_test_pulse_config.digital_only = digital_only
             self._c_test_pulse_config.external = external
             self._c_test_pulse_config.count = count
             self._c_test_pulse_config.period = period
             self._c_test_pulse_config.phase = phase
         else:
             self._c_test_pulse_config = cdata

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cconfig.katherine_test_pulse_config_snprint, &self._c_test_pulse_config)

    @property
    def enabled(self):
         return self._c_test_pulse_config.enabled

    @enabled.setter
    def enabled(self, val):
         self._c_test_pulse_config.enabled = val

    @property
    def digital_only(self):
         return self._c_test_pulse_config.digital_only

    @digital_only.setter
    def digital_only(self, val):
         self._c_test_pulse_config.digital_only = val

    @property
    def external(self):
         return self._c_test_pulse_config.external

    @external.setter
    def external(self, val):
         self._c_test_pulse_config.external = val

    @property
    def count(self):
         return self._c_test_pulse_config.count

    @count.setter
    def count(self, val):
         self._c_test_pulse_config.count = val

    @property
    def period(self):
         return self._c_test_pulse_config.period

    @period.setter
    def period(self, val):
         self._c_test_pulse_config.period = val

    @property
    def phase(self):
         return self._c_test_pulse_config.phase

    @phase.setter
    def phase(self, val):
         self._c_test_pulse_config.phase = val


cdef class Dacs:
    cdef cconfig.katherine_dacs_t _c_dacs

    def __init__(self, cdata=None):
         if cdata is not None:
             self._c_dacs.named = cdata['named']

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cconfig.katherine_dacs_snprint, &self._c_dacs)

    @property
    def Ibias_Preamp_ON(self):
       return self._c_dacs.named.Ibias_Preamp_ON

    @Ibias_Preamp_ON.setter
    def Ibias_Preamp_ON(self, val):
       self._c_dacs.named.Ibias_Preamp_ON = val

    @property
    def Ibias_Preamp_OFF(self):
       return self._c_dacs.named.Ibias_Preamp_OFF

    @Ibias_Preamp_OFF.setter
    def Ibias_Preamp_OFF(self, val):
       self._c_dacs.named.Ibias_Preamp_OFF = val

    @property
    def VPReamp_NCAS(self):
       return self._c_dacs.named.VPReamp_NCAS

    @VPReamp_NCAS.setter
    def VPReamp_NCAS(self, val):
       self._c_dacs.named.VPReamp_NCAS = val

    @property
    def Ibias_Ikrum(self):
       return self._c_dacs.named.Ibias_Ikrum

    @Ibias_Ikrum.setter
    def Ibias_Ikrum(self, val):
       self._c_dacs.named.Ibias_Ikrum = val

    @property
    def Vfbk(self):
       return self._c_dacs.named.Vfbk

    @Vfbk.setter
    def Vfbk(self, val):
       self._c_dacs.named.Vfbk = val

    @property
    def Vthreshold_fine(self):
       return self._c_dacs.named.Vthreshold_fine

    @Vthreshold_fine.setter
    def Vthreshold_fine(self, val):
       self._c_dacs.named.Vthreshold_fine = val

    @property
    def Vthreshold_coarse(self):
       return self._c_dacs.named.Vthreshold_coarse

    @Vthreshold_coarse.setter
    def Vthreshold_coarse(self, val):
       self._c_dacs.named.Vthreshold_coarse = val

    @property
    def Ibias_DiscS1_ON(self):
       return self._c_dacs.named.Ibias_DiscS1_ON

    @Ibias_DiscS1_ON.setter
    def Ibias_DiscS1_ON(self, val):
       self._c_dacs.named.Ibias_DiscS1_ON = val

    @property
    def Ibias_DiscS1_OFF(self):
       return self._c_dacs.named.Ibias_DiscS1_OFF

    @Ibias_DiscS1_OFF.setter
    def Ibias_DiscS1_OFF(self, val):
       self._c_dacs.named.Ibias_DiscS1_OFF = val

    @property
    def Ibias_DiscS2_ON(self):
       return self._c_dacs.named.Ibias_DiscS2_ON

    @Ibias_DiscS2_ON.setter
    def Ibias_DiscS2_ON(self, val):
       self._c_dacs.named.Ibias_DiscS2_ON = val

    @property
    def Ibias_DiscS2_OFF(self):
       return self._c_dacs.named.Ibias_DiscS2_OFF

    @Ibias_DiscS2_OFF.setter
    def Ibias_DiscS2_OFF(self, val):
       self._c_dacs.named.Ibias_DiscS2_OFF = val

    @property
    def Ibias_PixelDAC(self):
       return self._c_dacs.named.Ibias_PixelDAC

    @Ibias_PixelDAC.setter
    def Ibias_PixelDAC(self, val):
       self._c_dacs.named.Ibias_PixelDAC = val

    @property
    def Ibias_TPbufferIn(self):
       return self._c_dacs.named.Ibias_TPbufferIn

    @Ibias_TPbufferIn.setter
    def Ibias_TPbufferIn(self, val):
       self._c_dacs.named.Ibias_TPbufferIn = val

    @property
    def Ibias_TPbufferOut(self):
       return self._c_dacs.named.Ibias_TPbufferOut

    @Ibias_TPbufferOut.setter
    def Ibias_TPbufferOut(self, val):
       self._c_dacs.named.Ibias_TPbufferOut = val

    @property
    def VTP_coarse(self):
       return self._c_dacs.named.VTP_coarse

    @VTP_coarse.setter
    def VTP_coarse(self, val):
       self._c_dacs.named.VTP_coarse = val

    @property
    def VTP_fine(self):
       return self._c_dacs.named.VTP_fine

    @VTP_fine.setter
    def VTP_fine(self, val):
       self._c_dacs.named.VTP_fine = val

    @property
    def Ibias_CP_PLL(self):
       return self._c_dacs.named.Ibias_CP_PLL

    @Ibias_CP_PLL.setter
    def Ibias_CP_PLL(self, val):
       self._c_dacs.named.Ibias_CP_PLL = val

    @property
    def PLL_Vcntrl(self):
       return self._c_dacs.named.PLL_Vcntrl

    @PLL_Vcntrl.setter
    def PLL_Vcntrl(self, val):
       self._c_dacs.named.PLL_Vcntrl = val


cdef class PxConfig:
    cdef cpx_config.katherine_px_config_t _c_px_config

    def __init__(self, cdata=None):
      if cdata is not None:
         self._c_px_config = cdata

    def __repr__(self):
      return _snprint_repr(<snprint_fn_t> cpx_config.katherine_px_config_snprint, &self._c_px_config)

    @staticmethod
    def from_bmc(path):
      cdef cpx_config.katherine_px_config_t config
      res = cpx_config.katherine_px_config_load_bmc_file(&config, path.encode())
      check_return_code(res)
      return PxConfig(cdata=config)

    @staticmethod
    def from_bpc(path):
      cdef cpx_config.katherine_px_config_t config
      res = cpx_config.katherine_px_config_load_bpc_file(&config, path.encode())
      check_return_code(res)
      return PxConfig(cdata=config)

    @staticmethod
    def from_bmc_data(data):
      cdef const unsigned char[::1] view = data
      if view.shape[0] != sizeof(cpx_config.katherine_bmc_t):
         raise ValueError('BMC data must be exactly %d bytes long' % sizeof(cpx_config.katherine_bmc_t))
      cdef cpx_config.katherine_px_config_t config
      res = cpx_config.katherine_px_config_load_bmc_data(&config, <const cpx_config.katherine_bmc_t *> &view[0])
      check_return_code(res)
      return PxConfig(cdata=config)

    @staticmethod
    def from_bpc_data(data):
      cdef const unsigned char[::1] view = data
      if view.shape[0] != sizeof(cpx_config.katherine_bpc_t):
         raise ValueError('BPC data must be exactly %d bytes long' % sizeof(cpx_config.katherine_bpc_t))
      cdef cpx_config.katherine_px_config_t config
      res = cpx_config.katherine_px_config_load_bpc_data(&config, <const cpx_config.katherine_bpc_t *> &view[0])
      check_return_code(res)
      return PxConfig(cdata=config)

    @staticmethod
    cdef cpx.katherine_coord_t _coord(int x, int y) except *:
      if not (0 <= x <= 255 and 0 <= y <= 255):
         raise ValueError('pixel coordinates must lie within 0 to 255')

      cdef cpx.katherine_coord_t coord
      coord.x = x
      coord.y = y
      return coord

    def set_test_bit(self, int x, int y, bool enabled):
      cpx_config.katherine_px_config_set_test_bit(&self._c_px_config, PxConfig._coord(x, y), enabled)

    def get_test_bit(self, int x, int y):
      return cpx_config.katherine_px_config_get_test_bit(&self._c_px_config, PxConfig._coord(x, y))

    def set_mask_bit(self, int x, int y, bool masked):
      cpx_config.katherine_px_config_set_mask_bit(&self._c_px_config, PxConfig._coord(x, y), masked)

    def get_mask_bit(self, int x, int y):
      return cpx_config.katherine_px_config_get_mask_bit(&self._c_px_config, PxConfig._coord(x, y))

    def set_loc_thl(self, int x, int y, uint8_t loc_thl):
      if loc_thl > 15:
         raise ValueError('local threshold adjustment must lie within 0 to 15')
      cpx_config.katherine_px_config_set_loc_thl(&self._c_px_config, PxConfig._coord(x, y), loc_thl)

    def get_loc_thl(self, int x, int y):
      return cpx_config.katherine_px_config_get_loc_thl(&self._c_px_config, PxConfig._coord(x, y))

@unique
class Phase(Enum):
    PHASE_1          = cconfig.katherine_phase_t.PHASE_1
    PHASE_2          = cconfig.katherine_phase_t.PHASE_2
    PHASE_4          = cconfig.katherine_phase_t.PHASE_4
    PHASE_8          = cconfig.katherine_phase_t.PHASE_8
    PHASE_16         = cconfig.katherine_phase_t.PHASE_16


@unique
class Freq(Enum):
    FREQ_40          = cconfig.katherine_freq_t.FREQ_40
    FREQ_80          = cconfig.katherine_freq_t.FREQ_80
    FREQ_160         = cconfig.katherine_freq_t.FREQ_160


@unique
class Tpx3Reg(Enum):
    TEST_PULSE_METHOD     = cconfig.katherine_tpx3_reg_t.TPX3_REG_TEST_PULSE_METHOD
    NUMBER_TEST_PULSES    = cconfig.katherine_tpx3_reg_t.TPX3_REG_NUMBER_TEST_PULSES
    OUT_BLOCK_CONFIG      = cconfig.katherine_tpx3_reg_t.TPX3_REG_OUT_BLOCK_CONFIG
    PLL_CONFIG            = cconfig.katherine_tpx3_reg_t.TPX3_REG_PLL_CONFIG
    GENERAL_CONFIG        = cconfig.katherine_tpx3_reg_t.TPX3_REG_GENERAL_CONFIG
    SLVS_CONFIG           = cconfig.katherine_tpx3_reg_t.TPX3_REG_SLVS_CONFIG
    POWER_PULSING_PATTERN = cconfig.katherine_tpx3_reg_t.TPX3_REG_POWER_PULSING_PATTERN
    SET_TIMER_LOW         = cconfig.katherine_tpx3_reg_t.TPX3_REG_SET_TIMER_LOW
    SET_TIMER_MID         = cconfig.katherine_tpx3_reg_t.TPX3_REG_SET_TIMER_MID
    SET_TIMER_HIGH        = cconfig.katherine_tpx3_reg_t.TPX3_REG_SET_TIMER_HIGH
    SENSE_DAC_SELECTOR    = cconfig.katherine_tpx3_reg_t.TPX3_REG_SENSE_DAC_SELECTOR
    EXT_DAC_SELECTOR      = cconfig.katherine_tpx3_reg_t.TPX3_REG_EXT_DAC_SELECTOR


cdef class Config:
    cdef cconfig.katherine_config_t _c_config

    def __init__(self):
         pass

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cconfig.katherine_config_snprint, &self._c_config)

    @property
    def bias_id(self):
       return self._c_config.bias_id

    @bias_id.setter
    def bias_id(self, val):
       self._c_config.bias_id = val

    @property
    def acq_time(self):
       return self._c_config.acq_time

    @acq_time.setter
    def acq_time(self, val):
       self._c_config.acq_time = val

    @property
    def no_frames(self):
       return self._c_config.no_frames

    @no_frames.setter
    def no_frames(self, val):
       self._c_config.no_frames = val

    @property
    def bias(self):
       return self._c_config.bias

    @bias.setter
    def bias(self, val):
       self._c_config.bias = val

    @property
    def start_trigger(self):
       return Trigger(cdata=self._c_config.start_trigger)

    cdef _set_start_trigger(self, Trigger val):
         memcpy(&self._c_config.start_trigger, &val._c_trigger, sizeof(self._c_config.start_trigger))

    @start_trigger.setter
    def start_trigger(self, val):
         self._set_start_trigger(val)

    @property
    def delayed_start(self):
       return self._c_config.delayed_start

    @delayed_start.setter
    def delayed_start(self, val):
       self._c_config.delayed_start = val

    @property
    def stop_trigger(self):
       return Trigger(cdata=self._c_config.stop_trigger)

    cdef _set_stop_trigger(self, Trigger val):
         memcpy(&self._c_config.stop_trigger, &val._c_trigger, sizeof(self._c_config.stop_trigger))

    @stop_trigger.setter
    def stop_trigger(self, val):
         self._set_stop_trigger(val)

    @property
    def gray_disable(self):
       return self._c_config.gray_disable

    @gray_disable.setter
    def gray_disable(self, val):
       self._c_config.gray_disable = val

    @property
    def polarity_holes(self):
       return self._c_config.polarity_holes

    @polarity_holes.setter
    def polarity_holes(self, val):
       self._c_config.polarity_holes = val

    @property
    def phase(self):
       return Phase(self._c_config.phase)

    @phase.setter
    def phase(self, val):
       self._c_config.phase = val.value

    @property
    def freq(self):
       return Freq(self._c_config.freq)

    @freq.setter
    def freq(self, val):
       self._c_config.freq = val.value

    @property
    def dacs(self):
       return Dacs(cdata=self._c_config.dacs)

    cdef _set_dacs(self, Dacs val):
         memcpy(&self._c_config.dacs, &val._c_dacs, sizeof(self._c_config.dacs))

    @dacs.setter
    def dacs(self, val):
         self._set_dacs(val)

    @property
    def pixel_config(self):
       return PxConfig(cdata=self._c_config.pixel_config)

    cdef _set_pixel_config(self, PxConfig val):
         memcpy(&self._c_config.pixel_config, &val._c_px_config, sizeof(self._c_config.pixel_config))

    @pixel_config.setter
    def pixel_config(self, val):
         self._set_pixel_config(val)

    @property
    def test_pulse_config(self):
       return TestPulseConfig(cdata=self._c_config.test_pulse_config)

    cdef _set_test_pulse_config(self, TestPulseConfig val):
         memcpy(&self._c_config.test_pulse_config, &val._c_test_pulse_config, sizeof(self._c_config.test_pulse_config))

    @test_pulse_config.setter
    def test_pulse_config(self, val):
         self._set_test_pulse_config(val)


@unique
class AcquisitionMode(Enum):
    TOA_TOT          = cconfig.katherine_acquisition_mode_t.ACQUISITION_MODE_TOA_TOT
    ONLY_TOA         = cconfig.katherine_acquisition_mode_t.ACQUISITION_MODE_ONLY_TOA
    EVENT_ITOT       = cconfig.katherine_acquisition_mode_t.ACQUISITION_MODE_EVENT_ITOT


@unique
class AcquisitionState(Enum):
    NOT_STARTED      = cacquisition.katherine_acquisition_state_t.ACQUISITION_NOT_STARTED
    RUNNING          = cacquisition.katherine_acquisition_state_t.ACQUISITION_RUNNING
    SUCCEEDED        = cacquisition.katherine_acquisition_state_t.ACQUISITION_SUCCEEDED
    TIMED_OUT        = cacquisition.katherine_acquisition_state_t.ACQUISITION_TIMED_OUT


def str_acquisition_status(status):
    if isinstance(status, AcquisitionState):
        status = status.value
    cdef const char *s = cacquisition.katherine_str_acquisition_status(status)
    return s.decode('UTF-8')


@unique
class ReadoutType(Enum):
    FRAME_BASED         = cacquisition.katherine_readout_type_t.READOUT_SEQUENTIAL
    DATA_DRIVEN         = cacquisition.katherine_readout_type_t.READOUT_DATA_DRIVEN


cdef class AcquisitionObserver:
    def frame_started(self, frame_idx):
        pass

    def frame_ended(self, frame_idx, completed, frame_info):
        pass

    def pixels_received(self, pixels):
        pass

    def data_received(self, data):
        pass


cdef class FrameInfo:
    cdef cacquisition.katherine_frame_info_t _c_info

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cacquisition.katherine_frame_info_snprint, &self._c_info)

    @property
    def received_pixels(self):
       return self._c_info.received_pixels

    @property
    def sent_pixels(self):
       return self._c_info.sent_pixels

    @property
    def lost_pixels(self):
       return self._c_info.lost_pixels

    @property
    def start_time(self):
       return self._c_info.start_time.d

    @property
    def end_time(self):
       return self._c_info.end_time.d

    @property
    def start_time_observed(self):
       return self._c_info.start_time_observed

    @property
    def end_time_observed(self):
       return self._c_info.end_time_observed

    @property
    def completed(self):
       return self._c_info.completed


cdef class PxFastToaTot:
    cdef cpx.katherine_px_f_toa_tot_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cpx.katherine_px_f_toa_tot_snprint, &self._c_px)

    @staticmethod
    def RAW_SIZE():
       return sizeof(cpx.katherine_px_f_toa_tot_t)

    @property
    def x(self):
       return self._c_px.coord.x

    @property
    def y(self):
       return self._c_px.coord.y

    @property
    def ftoa(self):
       return self._c_px.ftoa

    @property
    def toa(self):
       return self._c_px.toa

    @property
    def tot(self):
       return self._c_px.tot


cdef class PxToaTot:
    cdef cpx.katherine_px_toa_tot_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cpx.katherine_px_toa_tot_snprint, &self._c_px)

    @staticmethod
    def RAW_SIZE():
       return sizeof(cpx.katherine_px_toa_tot_t)

    @property
    def x(self):
       return self._c_px.coord.x

    @property
    def y(self):
       return self._c_px.coord.y

    @property
    def hit_count(self):
       return self._c_px.hit_count

    @property
    def toa(self):
       return self._c_px.toa

    @property
    def tot(self):
       return self._c_px.tot


cdef class PxFastToaOnly:
    cdef cpx.katherine_px_f_toa_only_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cpx.katherine_px_f_toa_only_snprint, &self._c_px)

    @staticmethod
    def RAW_SIZE():
       return sizeof(cpx.katherine_px_f_toa_only_t)

    @property
    def x(self):
       return self._c_px.coord.x

    @property
    def y(self):
       return self._c_px.coord.y

    @property
    def ftoa(self):
       return self._c_px.ftoa

    @property
    def toa(self):
       return self._c_px.toa


cdef class PxToaOnly:
    cdef cpx.katherine_px_toa_only_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cpx.katherine_px_toa_only_snprint, &self._c_px)

    @staticmethod
    def RAW_SIZE():
       return sizeof(cpx.katherine_px_toa_only_t)

    @property
    def x(self):
       return self._c_px.coord.x

    @property
    def y(self):
       return self._c_px.coord.y

    @property
    def hit_count(self):
       return self._c_px.hit_count

    @property
    def toa(self):
       return self._c_px.toa


cdef class PxFastEventItot:
    cdef cpx.katherine_px_f_event_itot_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cpx.katherine_px_f_event_itot_snprint, &self._c_px)

    @staticmethod
    def RAW_SIZE():
       return sizeof(cpx.katherine_px_f_event_itot_t)

    @property
    def x(self):
       return self._c_px.coord.x

    @property
    def y(self):
       return self._c_px.coord.y

    @property
    def hit_count(self):
       return self._c_px.hit_count

    @property
    def event_count(self):
       return self._c_px.event_count

    @property
    def integral_tot(self):
       return self._c_px.integral_tot


cdef class PxEventItot:
    cdef cpx.katherine_px_event_itot_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cpx.katherine_px_event_itot_snprint, &self._c_px)

    @staticmethod
    def RAW_SIZE():
       return sizeof(cpx.katherine_px_event_itot_t)

    @property
    def x(self):
       return self._c_px.coord.x

    @property
    def y(self):
       return self._c_px.coord.y

    @property
    def event_count(self):
       return self._c_px.event_count

    @property
    def integral_tot(self):
       return self._c_px.integral_tot


cdef class Acquisition:
    cdef cacquisition.katherine_acquisition_t* _c_acq
    cdef public AcquisitionObserver observer

    def __cinit__(self, Device dev, size_t md_buffer_size, size_t pixel_buffer_size, int report_timeout, int fail_timeout):
      self._c_acq = <cacquisition.katherine_acquisition_t*> PyMem_Malloc(sizeof(cacquisition.katherine_acquisition_t))
      if self._c_acq is NULL:
          raise MemoryError()

      res = cacquisition.katherine_acquisition_init(self._c_acq, dev._c_device, <void*> self, md_buffer_size, pixel_buffer_size, report_timeout, fail_timeout)
      check_return_code(res)

      self._c_acq.handlers.frame_started = _forward_frame_started
      self._c_acq.handlers.frame_ended = _forward_frame_ended
      self._c_acq.handlers.pixels_received = NULL
      self._c_acq.handlers.data_received = _forward_data_received
      self.observer = AcquisitionObserver()

    def __dealloc__(self):
      if self._c_acq is not NULL:
         cacquisition.katherine_acquisition_fini(self._c_acq)

      PyMem_Free(self._c_acq)

    def __repr__(self):
      return _snprint_repr(<snprint_fn_t> cacquisition.katherine_acquisition_snprint, self._c_acq)

    def begin(self, Config config, readout_type, acq_mode, bool fast_vco_enabled, bool decode_data=True):
      if fast_vco_enabled:
        if acq_mode == AcquisitionMode.TOA_TOT:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_f_toa_tot
        elif acq_mode == AcquisitionMode.ONLY_TOA:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_f_toa_only
        elif acq_mode == AcquisitionMode.EVENT_ITOT:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_f_event_itot
      else:
        if acq_mode == AcquisitionMode.TOA_TOT:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_toa_tot
        elif acq_mode == AcquisitionMode.ONLY_TOA:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_toa_only
        elif acq_mode == AcquisitionMode.EVENT_ITOT:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_event_itot

      res = cacquisition.katherine_acquisition_begin(self._c_acq, &config._c_config, readout_type.value, acq_mode.value, fast_vco_enabled, decode_data)
      check_return_code(res)

    def abort(self):
      res = cacquisition.katherine_acquisition_abort(self._c_acq)
      check_return_code(res)

    def stop(self):
      res = cacquisition.katherine_acquisition_stop(self._c_acq)
      check_return_code(res)
         
    def read(self):
      res = cacquisition.katherine_acquisition_read(self._c_acq)
      check_return_code(res)

    @property
    def state(self):
       return AcquisitionState(self._c_acq.state)

    @property
    def aborted(self):
       return self._c_acq.aborted

    @property
    def requested_frames(self):
       return self._c_acq.requested_frames

    @property
    def completed_frames(self):
       return self._c_acq.completed_frames

    @property
    def dropped_measurement_data(self):
       return self._c_acq.dropped_measurement_data

    @property
    def truncated_measurement_data(self):
       return self._c_acq.truncated_measurement_data

    @property
    def readout_mode(self):
       return ReadoutType(self._c_acq.readout_mode)

    @property
    def acq_mode(self):
       return AcquisitionMode(self._c_acq.acq_mode)

    @property
    def fast_vco_enabled(self):
       return self._c_acq.fast_vco_enabled

    @property
    def decode_data(self):
       return self._c_acq.decode_data

    @property
    def md_buffer_size(self):
       return self._c_acq.md_buffer_size

    @property
    def pixel_buffer_size(self):
       return self._c_acq.pixel_buffer_size

    @property
    def pixel_buffer_valid(self):
       return self._c_acq.pixel_buffer_valid

    @property
    def pixel_buffer_max_valid(self):
       return self._c_acq.pixel_buffer_max_valid

    @property
    def requested_frame_duration(self):
       return self._c_acq.requested_frame_duration

    @property
    def acq_start_time(self):
       return self._c_acq.acq_start_time

    @property
    def report_timeout(self):
       return self._c_acq.report_timeout

    @property
    def fail_timeout(self):
       return self._c_acq.fail_timeout

    @property
    def current_frame_info(self):
       cdef FrameInfo info = FrameInfo()
       memcpy(&info._c_info, &self._c_acq.current_frame_info, sizeof(info._c_info))
       return info

    @property
    def last_toa_offset(self):
       return self._c_acq.last_toa_offset

    @property
    def frame_active(self):
       return self._c_acq.frame_active


cdef void _forward_frame_started(void *user_ctx, int frame_idx) noexcept:
    (<Acquisition> user_ctx).observer.frame_started(frame_idx)

cdef void _forward_frame_ended(void *user_ctx, int frame_idx, bool completed, const cacquisition.katherine_frame_info_t *info) noexcept:
    py_info = FrameInfo()
    memcpy(&py_info._c_info, info, sizeof(py_info._c_info))
    (<Acquisition> user_ctx).observer.frame_ended(frame_idx, completed, py_info)

cdef void _forward_data_received(void *user_ctx, const char *data, size_t count) noexcept:
    (<Acquisition> user_ctx).observer.data_received(data[:count])

cdef void _forward_pixels_received_f_toa_tot(void *user_ctx, const void *px, size_t count) noexcept:
    cdef const cpx.katherine_px_f_toa_tot_t *dpx = <const cpx.katherine_px_f_toa_tot_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([PxFastToaTot(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_toa_tot(void *user_ctx, const void *px, size_t count) noexcept:
    cdef const cpx.katherine_px_toa_tot_t *dpx = <const cpx.katherine_px_toa_tot_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([PxToaTot(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_f_toa_only(void *user_ctx, const void *px, size_t count) noexcept:
    cdef const cpx.katherine_px_f_toa_only_t *dpx = <const cpx.katherine_px_f_toa_only_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([PxFastToaOnly(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_toa_only(void *user_ctx, const void *px, size_t count) noexcept:
    cdef const cpx.katherine_px_toa_only_t *dpx = <const cpx.katherine_px_toa_only_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([PxToaOnly(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_f_event_itot(void *user_ctx, const void *px, size_t count) noexcept:
    cdef const cpx.katherine_px_f_event_itot_t *dpx = <const cpx.katherine_px_f_event_itot_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([PxFastEventItot(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_event_itot(void *user_ctx, const void *px, size_t count) noexcept:
    cdef const cpx.katherine_px_event_itot_t *dpx = <const cpx.katherine_px_event_itot_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([PxEventItot(cdata=dpx[i]) for i in range(count)])

def MD_SIZE():
   return cacquisition.KATHERINE_MD_SIZE
