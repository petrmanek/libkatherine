# cython: language_level=3

# Katherine Control Library
#
# This file was created on 3.2.19 by Petr Manek.
# 
# Contents of this file are copyrighted and subject to license
# conditions specified in the LICENSE file located in the top
# directory.

from cpython.mem cimport PyMem_Malloc, PyMem_Free
from libc.string cimport memcpy
from libcpp cimport bool
from os import strerror
from enum import Enum, unique
import array

cimport cdevice
cimport cstatus
cimport cconfig
cimport cpx_config
cimport cacquisition


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


cdef class Trigger:
    cdef cconfig.katherine_trigger_t _c_trigger

    def __init__(self, enabled=False, channel=0, use_falling_edge=False, cdata=None):
         if cdata is None:
             self._c_trigger.enabled = enabled
             self._c_trigger.channel = channel
             self._c_trigger.use_falling_edge = use_falling_edge
         else:
             self._c_trigger = cdata

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


cdef class Dacs:
    cdef cconfig.katherine_dacs_t _c_dacs

    def __init__(self, cdata=None):
         if cdata is not None:
             self._c_dacs.named = cdata.named

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


cdef class Config:
    cdef cconfig.katherine_config_t _c_config

    def __init__(self):
         pass

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


cdef class FrameInfo:
    cdef cacquisition.katherine_frame_info_t _c_info

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


cdef class PxFastToaTot:
    cdef cacquisition.katherine_px_f_toa_tot_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    @staticmethod
    def RAW_SIZE():
       return sizeof(cacquisition.katherine_px_f_toa_tot_t)

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
    cdef cacquisition.katherine_px_toa_tot_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    @staticmethod
    def RAW_SIZE():
       return sizeof(cacquisition.katherine_px_toa_tot_t)

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
    cdef cacquisition.katherine_px_f_toa_only_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    @staticmethod
    def RAW_SIZE():
       return sizeof(cacquisition.katherine_px_f_toa_only_t)

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
    cdef cacquisition.katherine_px_toa_only_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    @staticmethod
    def RAW_SIZE():
       return sizeof(cacquisition.katherine_px_toa_only_t)

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
    cdef cacquisition.katherine_px_f_event_itot_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    @staticmethod
    def RAW_SIZE():
       return sizeof(cacquisition.katherine_px_f_event_itot_t)

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
    cdef cacquisition.katherine_px_event_itot_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    @staticmethod
    def RAW_SIZE():
       return sizeof(cacquisition.katherine_px_event_itot_t)

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
      self.observer = AcquisitionObserver()

    def __dealloc__(self):
      if self._c_acq is not NULL:
         cacquisition.katherine_acquisition_fini(self._c_acq)

      PyMem_Free(self._c_acq)
         
    def begin(self, Config config, readout_type, acq_mode, bool fast_vco_enabled):
      if fast_vco_enabled:
        if acq_mode == AcquisitionMode.TOA_TOT:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_f_toa_tot
        elif acq_mode == AcquisitionMode.TOA_ONLY:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_f_toa_only
        elif acq_mode == AcquisitionMode.EVENT_ITOT:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_f_event_itot
      else:
        if acq_mode == AcquisitionMode.TOA_TOT:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_toa_tot
        elif acq_mode == AcquisitionMode.TOA_ONLY:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_toa_only
        elif acq_mode == AcquisitionMode.EVENT_ITOT:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_event_itot
      
      res = cacquisition.katherine_acquisition_begin(self._c_acq, &config._c_config, readout_type.value, acq_mode.value, fast_vco_enabled)
      check_return_code(res)
      
    def abort(self):
      res = cacquisition.katherine_acquisition_abort(self._c_acq)
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


cdef void _forward_frame_started(void *user_ctx, int frame_idx):
    (<Acquisition> user_ctx).observer.frame_started(frame_idx)

cdef void _forward_frame_ended(void *user_ctx, int frame_idx, bool completed, const cacquisition.katherine_frame_info_t *info):
    py_info = FrameInfo()
    memcpy(&py_info._c_info, info, sizeof(py_info._c_info))
    (<Acquisition> user_ctx).observer.frame_ended(frame_idx, completed, py_info)

cdef void _forward_pixels_received_f_toa_tot(void *user_ctx, const void *px, size_t count):
    cdef const cacquisition.katherine_px_f_toa_tot_t *dpx = <const cacquisition.katherine_px_f_toa_tot_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([PxFastToaTot(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_toa_tot(void *user_ctx, const void *px, size_t count):
    cdef const cacquisition.katherine_px_toa_tot_t *dpx = <const cacquisition.katherine_px_toa_tot_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([PxToaTot(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_f_toa_only(void *user_ctx, const void *px, size_t count):
    cdef const cacquisition.katherine_px_f_toa_only_t *dpx = <const cacquisition.katherine_px_f_toa_only_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([PxFastToaOnly(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_toa_only(void *user_ctx, const void *px, size_t count):
    cdef const cacquisition.katherine_px_toa_only_t *dpx = <const cacquisition.katherine_px_toa_only_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([PxToaOnly(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_f_event_itot(void *user_ctx, const void *px, size_t count):
    cdef const cacquisition.katherine_px_f_event_itot_t *dpx = <const cacquisition.katherine_px_f_event_itot_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([PxFastEventItot(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_event_itot(void *user_ctx, const void *px, size_t count):
    cdef const cacquisition.katherine_px_event_itot_t *dpx = <const cacquisition.katherine_px_event_itot_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([PxEventItot(cdata=dpx[i]) for i in range(count)])

def MD_SIZE():
   return cacquisition.KATHERINE_MD_SIZE
