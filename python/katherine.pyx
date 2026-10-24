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
from libc.stdint cimport uint8_t, uint16_t, uint32_t, uint64_t, int32_t
from libc.stdlib cimport calloc, free
from libc.string cimport memcpy, memset
from libcpp cimport bool
# Imported under private names on purpose. A module-scope `from enum import
# Enum` makes katherine.Enum a public attribute of this module and pulls it
# into `from katherine import *`, which is no part of this binding's surface;
# the underscore keeps both out, `import *` skipping underscored names by
# itself. Hence no __all__: the public surface is everything not underscored,
# which is the rule Python already applies.
from enum import Enum as _Enum, IntEnum as _IntEnum, unique as _unique
import array as _array
import warnings as _warnings

cimport cdevice
cimport ctransport
cimport cstatus
cimport cconfig
cimport cgpio
cimport ctdc
cimport cpulse
cimport cpx
cimport cpx_config
cimport cacquisition
cimport ctoa
cimport cversion
cimport cerror


@_unique
class ErrorCode(_IntEnum):
    """Every code the library returns.

    An IntEnum, so that a caught code compares against a bare number as well
    as against a name."""
    OK          = cerror.katherine_error_t.KATHERINE_E_OK
    TIMEOUT     = cerror.katherine_error_t.KATHERINE_E_TIMEOUT
    IO          = cerror.katherine_error_t.KATHERINE_E_IO
    CLOSED      = cerror.katherine_error_t.KATHERINE_E_CLOSED
    ADDRESS     = cerror.katherine_error_t.KATHERINE_E_ADDRESS
    BAD_CRD     = cerror.katherine_error_t.KATHERINE_E_BAD_CRD
    STRAY       = cerror.katherine_error_t.KATHERINE_E_STRAY
    PROTO       = cerror.katherine_error_t.KATHERINE_E_PROTO
    UNSUPPORTED = cerror.katherine_error_t.KATHERINE_E_UNSUPPORTED
    BAD_CHIP    = cerror.katherine_error_t.KATHERINE_E_BAD_CHIP
    STATE       = cerror.katherine_error_t.KATHERINE_E_STATE
    HW_UNKNOWN  = cerror.katherine_error_t.KATHERINE_E_HW_UNKNOWN
    INVAL       = cerror.katherine_error_t.KATHERINE_E_INVAL
    NOMEM       = cerror.katherine_error_t.KATHERINE_E_NOMEM
    SYSTEM      = cerror.katherine_error_t.KATHERINE_E_SYSTEM

    def __str__(self):
        """The description katherine_strerror() gives this code."""
        return cerror.katherine_strerror(self.value).decode('UTF-8')


class Error(RuntimeError):
    """A failure the library reported, carrying the code it reported.

    Every exception this binding raises for a library return code is one of
    these, so `except katherine.Error` catches all of them and `.code` says
    which. The three with a Python meaning of their own are also subclasses
    of it, so code written against the plain builtins keeps working."""

    def __init__(self, code, message=None):
        self.code = ErrorCode(code)
        super().__init__(message if message is not None else str(self.code))


class Timeout(Error, TimeoutError):
    """ErrorCode.TIMEOUT, and a builtin TimeoutError as well."""


class InvalidArgument(Error, ValueError):
    """ErrorCode.INVAL, and a builtin ValueError as well."""


class OutOfMemory(Error, MemoryError):
    """ErrorCode.NOMEM, and a builtin MemoryError as well."""


def check_return_code(int res):
    if res == 0:
        return

    message = cerror.katherine_strerror(res).decode('UTF-8')

    if res == cerror.KATHERINE_E_TIMEOUT:
        raise Timeout(res, message)
    elif res == cerror.KATHERINE_E_INVAL:
        raise InvalidArgument(res, message)
    elif res == cerror.KATHERINE_E_NOMEM:
        raise OutOfMemory(res, message)
    else:
        raise Error(res, message)


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


DISCOVER_MIN_PORT = cdevice.KATHERINE_DISCOVER_MIN_PORT


IF KATHERINE_FEATURE_UDP:
    def discover_udp(address, uint16_t reply_port, uint32_t timeout_ms, size_t capacity=32):
        """Ask every readout reachable at an address to identify itself.

        One echo-chip-id command, then whatever answers arrive until the
        timeout. Returns (address, legacy) pairs in the order they arrived,
        where legacy is True for a firmware that ignored the reply port and
        answered on the standard one; an empty list means nothing answered,
        which is not an error. The reply port must be at least
        DISCOVER_MIN_PORT and may not be the standard port.

        Each address names its link -- "udp://192.168.1.42" -- so one can be
        passed straight to Device(). The address asked may name its link too;
        one without a scheme is a UDP address.
        """
        cdef bytes address_b = address.encode('UTF-8')
        cdef cdevice.katherine_discovered_t *out
        cdef size_t count = capacity

        if capacity == 0:
            raise ValueError('capacity must be positive')

        out = <cdevice.katherine_discovered_t *> PyMem_Malloc(
            capacity * sizeof(cdevice.katherine_discovered_t))
        if out is NULL:
            raise MemoryError()

        try:
            res = cdevice.katherine_device_discover_udp(address_b, reply_port, timeout_ms, out, &count)
            check_return_code(res)
            return [(out[i].address.decode('UTF-8'), out[i].legacy) for i in range(count)]
        finally:
            PyMem_Free(out)

IF KATHERINE_FEATURE_USB:
    def discover_usb(size_t capacity=32):
        """Ask the bus which readouts are attached.

        Nothing is opened: the walk reads descriptors and topology only, so
        it works before a udev rule grants access. That is the difference
        from discover_udp(), which has to put a datagram on the wire.

        Returns (address, legacy) pairs, one per readout attached. Each
        address names its link -- "usb://2-3" -- so one can be passed
        straight to Device(). legacy is always False on this link: it
        describes how a firmware answered a discovery datagram, and no
        datagram is sent here.
        """
        cdef cdevice.katherine_discovered_t *out
        cdef size_t count = capacity

        if capacity == 0:
            raise ValueError('capacity must be positive')

        out = <cdevice.katherine_discovered_t *> PyMem_Malloc(
            capacity * sizeof(cdevice.katherine_discovered_t))
        if out is NULL:
            raise MemoryError()

        try:
            res = cdevice.katherine_device_discover_usb(out, &count)
            check_return_code(res)
            return [(out[i].address.decode('UTF-8'), out[i].legacy) for i in range(count)]
        finally:
            PyMem_Free(out)


def version():
    return cversion.katherine_version()


# ------------------------------------------------------------------
# Sizes and units the readout's peripherals are counted in, taken from the
# library rather than repeated: a caller sizing a channel list or converting
# a tick reads them here.

GPIO_COUNT = cgpio.KATHERINE_MAX_GPIO_COUNT
GPIO_TICK_NS = cgpio.KATHERINE_GPIO_TICK_NS
TDC_CHANNEL_COUNT = ctdc.KATHERINE_MAX_TDC_CHANNEL_COUNT
PULSE_CHANNEL_COUNT = cpulse.KATHERINE_MAX_PULSE_CHANNEL_COUNT
TPX3_DAC_MONITOR_COUNT = cconfig.KATHERINE_TPX3_DAC_MONITOR_COUNT
TPX3_DAC_COUNT = cconfig.KATHERINE_TPX3_DAC_COUNT
MAX_BIAS_SUPPLIES = cconfig.KATHERINE_CONFIG_MAX_BIAS_SUPPLIES


@_unique
class GpioMapping(_Enum):
    """What a GPIO channel is, and what it carries as an output."""
    INPUT              = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_INPUT
    LOW                = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_LOW
    PULSE_INTERNAL     = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_PULSE_INTERNAL
    PULSE_CHANNEL_0    = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_PULSE_CHANNEL_0
    PULSE_CHANNEL_1    = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_PULSE_CHANNEL_1
    PULSE_CHANNEL_2    = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_PULSE_CHANNEL_2
    LEVEL_0            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_LEVEL_0
    LEVEL_1            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_LEVEL_1
    LEVEL_2            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_LEVEL_2
    LEVEL_3            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_LEVEL_3
    LEVEL_4            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_LEVEL_4
    LEVEL_5            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_LEVEL_5
    LEVEL_6            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_LEVEL_6
    LEVEL_7            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_LEVEL_7
    READY_LOCAL        = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_READY_LOCAL
    READY_COMPOSED     = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_READY_COMPOSED
    EVENT_TRIGGER      = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_EVENT_TRIGGER
    CHIP_0_PIXEL_DATA  = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_CHIP_0_PIXEL_DATA
    CHIP_1_PIXEL_DATA  = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_CHIP_1_PIXEL_DATA
    CHIP_2_PIXEL_DATA  = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_CHIP_2_PIXEL_DATA
    CHIP_3_PIXEL_DATA  = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_CHIP_3_PIXEL_DATA
    SHUTTER            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_SHUTTER
    CASCADE_CHAIN      = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_CASCADE_CHAIN
    CLOCK              = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_CLOCK
    SYNC_SIGNAL        = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_SYNC_SIGNAL
    SPARE_0            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_SPARE_0
    SPARE_1            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_SPARE_1
    SPARE_2            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_SPARE_2
    SPARE_3            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_SPARE_3
    SPARE_4            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_SPARE_4
    SPARE_5            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_SPARE_5
    SPARE_6            = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_SPARE_6
    HIGH               = cgpio.katherine_gpio_mapping_t.KATHERINE_GPIO_HIGH

    def __str__(self):
        """Render as the C library does, e.g. 'shutter'."""
        return cgpio.katherine_str_gpio_mapping(self.value).decode('UTF-8')


@_unique
class GpioReady(_Enum):
    """How a GPIO channel takes part in the composed ready signal."""
    UNUSED    = cgpio.katherine_gpio_ready_t.KATHERINE_GPIO_READY_UNUSED
    WHEN_HIGH = cgpio.katherine_gpio_ready_t.KATHERINE_GPIO_READY_WHEN_HIGH
    WHEN_LOW  = cgpio.katherine_gpio_ready_t.KATHERINE_GPIO_READY_WHEN_LOW


@_unique
class GpioMonitorSignal(_Enum):
    """What a GPIO channel's level is doing."""
    UNKNOWN  = cgpio.katherine_gpio_monitor_signal_t.KATHERINE_GPIO_MONITOR_SIGNAL_UNKNOWN
    CONSTANT = cgpio.katherine_gpio_monitor_signal_t.KATHERINE_GPIO_MONITOR_SIGNAL_CONSTANT
    PERIODIC = cgpio.katherine_gpio_monitor_signal_t.KATHERINE_GPIO_MONITOR_SIGNAL_PERIODIC

    def __str__(self):
        """Render as the C library does, e.g. 'periodic'."""
        return cgpio.katherine_str_gpio_monitor_signal(self.value).decode('UTF-8')


@_unique
class TdcSource(_Enum):
    """What a timing channel listens to."""
    GPIO_0           = ctdc.katherine_tdc_source_t.KATHERINE_TDC_SOURCE_GPIO_0
    GPIO_1           = ctdc.katherine_tdc_source_t.KATHERINE_TDC_SOURCE_GPIO_1
    GPIO_2           = ctdc.katherine_tdc_source_t.KATHERINE_TDC_SOURCE_GPIO_2
    GPIO_3           = ctdc.katherine_tdc_source_t.KATHERINE_TDC_SOURCE_GPIO_3
    CHIP_0_FEEDBACK  = ctdc.katherine_tdc_source_t.KATHERINE_TDC_SOURCE_CHIP_0_FEEDBACK
    CHIP_1_FEEDBACK  = ctdc.katherine_tdc_source_t.KATHERINE_TDC_SOURCE_CHIP_1_FEEDBACK
    CHIP_2_FEEDBACK  = ctdc.katherine_tdc_source_t.KATHERINE_TDC_SOURCE_CHIP_2_FEEDBACK
    CHIP_3_FEEDBACK  = ctdc.katherine_tdc_source_t.KATHERINE_TDC_SOURCE_CHIP_3_FEEDBACK

    def __str__(self):
        """Render as the C library does, e.g. 'gpio_2'."""
        return ctdc.katherine_str_tdc_source(self.value).decode('UTF-8')


@_unique
class PulseSource(_Enum):
    """Where the pulse generator takes its trigger."""

    RISING_EDGE  = cpulse.katherine_pulse_source_t.KATHERINE_PULSE_SOURCE_RISING_EDGE
    FALLING_EDGE = cpulse.katherine_pulse_source_t.KATHERINE_PULSE_SOURCE_FALLING_EDGE
    OSCILLATOR   = cpulse.katherine_pulse_source_t.KATHERINE_PULSE_SOURCE_OSCILLATOR

    def __str__(self):
        """Render as the C library does, e.g. 'oscillator'."""
        return cpulse.katherine_str_pulse_source(self.value).decode('UTF-8')


@_unique
class PulseOutput(_Enum):
    """What a generator channel puts out."""
    PULSE                = cpulse.katherine_pulse_output_t.KATHERINE_PULSE_OUTPUT_PULSE
    PULSE_INVERTED       = cpulse.katherine_pulse_output_t.KATHERINE_PULSE_OUTPUT_PULSE_INVERTED
    PASSTHROUGH          = cpulse.katherine_pulse_output_t.KATHERINE_PULSE_OUTPUT_PASSTHROUGH
    PASSTHROUGH_INVERTED = cpulse.katherine_pulse_output_t.KATHERINE_PULSE_OUTPUT_PASSTHROUGH_INVERTED

    def __str__(self):
        """Render as the C library does, e.g. 'pulse_inverted'."""
        return cpulse.katherine_str_pulse_output(self.value).decode('UTF-8')


@_unique
class DacUnit(_Enum):
    """The physical quantity a DAC sets."""
    AMP  = cconfig.katherine_dac_unit_t.KATHERINE_DAC_UNIT_AMP
    VOLT = cconfig.katherine_dac_unit_t.KATHERINE_DAC_UNIT_VOLT

    def __str__(self):
        return 'A' if self is DacUnit.AMP else 'V'


@_unique
class Tpx3Dac(_Enum):
    """The chip's eighteen DACs, in the order katherine_tpx3_dacs_t stores
    them. The first eighteen DAC monitors are these unchanged."""
    IBIAS_PREAMP_ON    = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_IBIAS_PREAMP_ON
    IBIAS_PREAMP_OFF   = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_IBIAS_PREAMP_OFF
    VPREAMP_NCAS       = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_VPREAMP_NCAS
    IBIAS_IKRUM        = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_IBIAS_IKRUM
    VFBK               = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_VFBK
    VTHRESHOLD_FINE    = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_VTHRESHOLD_FINE
    VTHRESHOLD_COARSE  = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_VTHRESHOLD_COARSE
    IBIAS_DISCS1_ON    = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_IBIAS_DISCS1_ON
    IBIAS_DISCS1_OFF   = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_IBIAS_DISCS1_OFF
    IBIAS_DISCS2_ON    = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_IBIAS_DISCS2_ON
    IBIAS_DISCS2_OFF   = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_IBIAS_DISCS2_OFF
    IBIAS_PIXELDAC     = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_IBIAS_PIXELDAC
    IBIAS_TPBUFFERIN   = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_IBIAS_TPBUFFERIN
    IBIAS_TPBUFFEROUT  = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_IBIAS_TPBUFFEROUT
    VTP_COARSE         = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_VTP_COARSE
    VTP_FINE           = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_VTP_FINE
    IBIAS_CP_PLL       = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_IBIAS_CP_PLL
    PLL_VCNTRL         = cconfig.katherine_tpx3_dac_t.KATHERINE_TPX3_DAC_PLL_VCNTRL

    def __str__(self):
        """Render as the C library does, e.g. 'Vthreshold_fine'."""
        return cconfig.katherine_tpx3_dac_name(self.value).decode('UTF-8')

    @property
    def max(self):
        """Largest code this DAC's register holds."""
        return cconfig.katherine_tpx3_dac_max(self.value)

    def to_si(self, uint16_t value):
        """The quantity a code sets, as (magnitude, DacUnit)."""
        cdef cconfig.katherine_dac_unit_t unit
        magnitude = cconfig.katherine_tpx3_dac_to_si(self.value, value, &unit)
        return magnitude, DacUnit(unit)


@_unique
class Tpx3DacMonitor(_Enum):
    """The four monitoring outputs past the DACs themselves. The first
    eighteen monitors are Tpx3Dac values unchanged."""
    BANDGAP_OUTPUT = cconfig.katherine_tpx3_dac_monitor_t.KATHERINE_TPX3_DAC_MONITOR_BANDGAP_OUTPUT
    BANDGAP_TEMP   = cconfig.katherine_tpx3_dac_monitor_t.KATHERINE_TPX3_DAC_MONITOR_BANDGAP_TEMP
    IBIAS_DAC      = cconfig.katherine_tpx3_dac_monitor_t.KATHERINE_TPX3_DAC_MONITOR_IBIAS_DAC
    IBIAS_DAC_CAS  = cconfig.katherine_tpx3_dac_monitor_t.KATHERINE_TPX3_DAC_MONITOR_IBIAS_DAC_CAS

    def __str__(self):
        """Render as the C library does, e.g. 'BandGap output'."""
        return cconfig.katherine_tpx3_dac_monitor_name(self.value).decode('UTF-8')


# ------------------------------------------------------------------
# The readout's own peripherals. Their configurations are sequences of small
# Python objects rather than wrapper classes holding a C array: a channel
# configuration is three plain fields, and a class per channel would be more
# code than the conversion it saves.


class GpioChannelConfig:
    """One GPIO channel's setting: what it is, whether the readout terminates
    it, and how it takes part in the composed ready signal."""

    def __init__(self, mapping=None, termination=False, ready=None):
        self.mapping = GpioMapping.INPUT if mapping is None else mapping
        self.termination = termination
        self.ready = GpioReady.UNUSED if ready is None else ready

    def __repr__(self):
        return 'GpioChannelConfig(mapping=%s, termination=%r, ready=%s)' % (
            self.mapping, self.termination, self.ready)


class TdcChannelConfig:
    """One timing channel's setting. The source does not reach a
    first-generation readout, whose firmware has no field for it."""

    def __init__(self, enabled=False, source=None, edge=None):
        self.enabled = enabled
        self.source = TdcSource.GPIO_0 if source is None else source
        self.edge = TriggerEdge.RISING if edge is None else TriggerEdge(edge)

    def __repr__(self):
        return 'TdcChannelConfig(enabled=%r, source=%s, edge=%s)' % (
            self.enabled, self.source, self.edge)


cdef void _fill_gpio_config(cgpio.katherine_gpio_config_t *config, channels) except *:
    cdef int i
    memset(config, 0, sizeof(cgpio.katherine_gpio_config_t))
    if len(channels) > GPIO_COUNT:
        raise ValueError('at most %d channels' % GPIO_COUNT)
    for i in range(len(channels)):
        config.channel[i].mapping = channels[i].mapping.value
        config.channel[i].termination = channels[i].termination
        config.channel[i].ready = channels[i].ready.value


cdef void _fill_tdc_config(ctdc.katherine_tdc_config_t *config, channels) except *:
    cdef int i
    memset(config, 0, sizeof(ctdc.katherine_tdc_config_t))
    if len(channels) > TDC_CHANNEL_COUNT:
        raise ValueError('at most %d channels' % TDC_CHANNEL_COUNT)
    for i in range(len(channels)):
        config.channel[i].enabled = channels[i].enabled
        config.channel[i].source = channels[i].source.value
        config.channel[i].edge = TriggerEdge(channels[i].edge).value


cdef class GpioMonitor:
    """What every GPIO channel reads, as katherine_probe_gpio() filled it."""
    cdef cgpio.katherine_gpio_monitor_t _c_state

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cgpio.katherine_gpio_monitor_snprint, &self._c_state)

    def level(self, int channel):
         self._check(channel)
         return self._c_state.channel[channel].level

    def signal(self, int channel):
         self._check(channel)
         return GpioMonitorSignal(self._c_state.channel[channel].signal)

    def high_ticks(self, int channel):
         self._check(channel)
         return self._c_state.channel[channel].high_ticks

    def low_ticks(self, int channel):
         self._check(channel)
         return self._c_state.channel[channel].low_ticks

    def frequency(self, int channel):
         """Hertz, or zero where the readout measured no period."""
         self._check(channel)
         return cgpio.katherine_gpio_channel_frequency(&self._c_state.channel[channel])

    def duty_cycle(self, int channel):
         self._check(channel)
         return cgpio.katherine_gpio_channel_duty_cycle(&self._c_state.channel[channel])

    def _check(self, int channel):
         if channel < 0 or channel >= GPIO_COUNT:
             raise IndexError('channel out of range')


cdef class PulseConfig:
    """What the pulse generator is set to."""
    cdef cpulse.katherine_pulse_config_t _c_config

    def __init__(self, channels=None, only_when_ready=False,
                 source=None, oscillator_frequency_hz=0.0):
         cdef int i
         memset(&self._c_config, 0, sizeof(cpulse.katherine_pulse_config_t))
         self._c_config.only_when_ready = only_when_ready
         self._c_config.source = (PulseSource.RISING_EDGE if source is None else source).value
         self._c_config.oscillator_frequency_hz = oscillator_frequency_hz
         if channels is not None:
             if len(channels) > PULSE_CHANNEL_COUNT:
                 raise ValueError('at most %d channels' % PULSE_CHANNEL_COUNT)
             for i in range(len(channels)):
                 self._c_config.channel[i].enabled = channels[i].get('enabled', False)
                 self._c_config.channel[i].output = channels[i].get(
                     'output', PulseOutput.PULSE).value
                 self._c_config.channel[i].length_seconds = channels[i].get('length_seconds', 0.0)
                 self._c_config.channel[i].delay_seconds = channels[i].get('delay_seconds', 0.0)


cdef class PulseCounts:
    """What the generator reports about the trigger reaching it."""
    cdef cpulse.katherine_pulse_counts_t _c_counts

    def __repr__(self):
         return ('PulseCounts(low_seconds=%g, high_seconds=%g, rising_edges=%d, clock_mhz=%d)'
                 % (self._c_counts.low_seconds, self._c_counts.high_seconds,
                    self._c_counts.rising_edges, self._c_counts.clock_mhz))

    @property
    def low_seconds(self):
         return self._c_counts.low_seconds

    @property
    def high_seconds(self):
         return self._c_counts.high_seconds

    @property
    def rising_edges(self):
         return self._c_counts.rising_edges

    @property
    def clock_mhz(self):
         """The generator's own clock, which is what makes its resolution self-describing."""
         return self._c_counts.clock_mhz


cdef class Tpx3DacVoltages:
    """What the readout measured at every DAC monitor, in Volts."""
    cdef cconfig.katherine_tpx3_dac_voltages_t _c_voltages

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cconfig.katherine_tpx3_dac_voltages_snprint, &self._c_voltages)

    def __len__(self):
         return TPX3_DAC_MONITOR_COUNT

    def __getitem__(self, int monitor):
         if monitor < 0 or monitor >= TPX3_DAC_MONITOR_COUNT:
             raise IndexError('monitor out of range')
         return self._c_voltages.array[monitor]

    def name(self, int monitor):
         if monitor < 0 or monitor >= TPX3_DAC_MONITOR_COUNT:
             raise IndexError('monitor out of range')
         return cconfig.katherine_tpx3_dac_monitor_name(<cconfig.katherine_tpx3_dac_monitor_t> monitor).decode('UTF-8')


@_unique
class ChipType(_Enum):
    """Timepix chip a readout drives."""
    UNKNOWN = cdevice.katherine_chip_type_t.KATHERINE_CHIP_UNKNOWN
    TPX2    = cdevice.katherine_chip_type_t.KATHERINE_CHIP_TPX2
    TPX3    = cdevice.katherine_chip_type_t.KATHERINE_CHIP_TPX3
    TPX4    = cdevice.katherine_chip_type_t.KATHERINE_CHIP_TPX4

    def __str__(self):
        """Render as the C library does, e.g. 'tpx3'."""
        return cdevice.katherine_str_chip_type(self.value).decode('UTF-8')


class DeviceFlags:
    """What opening a readout may be told not to do.

    Combined with |. An open with all three sets up the two sockets and
    nothing else, sending no command and waiting for no acknowledgement, so
    it succeeds whether or not a readout is there.

    A plain class rather than an enum.Flag: these are passed to C as a bare
    uint32_t, and a caller writing DONT_ENUMERATE | DONT_SELECT_INTERFACE
    should get an int back without a cast."""
    DONT_ENUMERATE        = cdevice.katherine_device_flags_t.KATHERINE_DEVICE_DONT_ENUMERATE
    DONT_CHANGE_UDP_PORTS = cdevice.katherine_device_flags_t.KATHERINE_DEVICE_DONT_CHANGE_UDP_PORTS
    DONT_SELECT_INTERFACE = cdevice.katherine_device_flags_t.KATHERINE_DEVICE_DONT_SELECT_INTERFACE


cdef class DeviceInfo:
    """What a readout says it is: facts it reported, or that a caller
    declared on its behalf. Nothing here is inferred."""
    cdef cdevice.katherine_device_info_t _c_info

    def __init__(self, hw_type=0, hw_revision=0, serial_number=0,
                 fw_version=0, chip_count=0, legacy=False):
         self._c_info.hw_type       = hw_type
         self._c_info.hw_revision   = hw_revision
         self._c_info.serial_number = serial_number
         self._c_info.fw_version    = fw_version
         self._c_info.chip_count    = chip_count
         self._c_info.legacy        = legacy

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cdevice.katherine_device_info_snprint, &self._c_info)

    @property
    def hw_type(self):
         """Reported hardware type. 0 means nothing has been enumerated or
         declared yet."""
         return self._c_info.hw_type

    @property
    def hw_revision(self):
         return self._c_info.hw_revision

    @property
    def serial_number(self):
         return self._c_info.serial_number

    @property
    def fw_version(self):
         return self._c_info.fw_version

    @property
    def chip_count(self):
         """The number of chips answering when this was taken. Compare
         against DeviceDerivedInfo.max_chip_count for what the hardware could
         possibly carry."""
         return self._c_info.chip_count

    @property
    def legacy(self):
         """Whether the firmware ignores the UDP echo-port extension, and so
         does not honour a port change either."""
         return self._c_info.legacy


cdef class DeviceDerivedInfo:
    """What a readout's reported hardware type means. Zeroed, with supported
    false, until the readout has been asked or a caller has declared it."""
    cdef cdevice.katherine_device_derived_info_t _c_info

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cdevice.katherine_device_derived_info_snprint, &self._c_info)

    @property
    def name(self):
         """Human-readable readout name, or None where the type is unknown."""
         return self._c_info.name.decode('UTF-8') if self._c_info.name is not NULL else None

    @property
    def chip_type(self):
         """Timepix chip this readout drives."""
         return ChipType(self._c_info.chip_type)

    @property
    def gen(self):
         return self._c_info.gen

    @property
    def max_chip_count(self):
         return self._c_info.max_chip_count

    @property
    def bias_supply_count(self):
         return self._c_info.bias_supply_count

    @property
    def accessible_gpio_count(self):
         return self._c_info.accessible_gpio_count

    @property
    def all_gpio_count(self):
         return self._c_info.all_gpio_count

    @property
    def tdc_channel_count(self):
         """Channels watched by the readout's timing unit, each with a
         counter of its own."""
         return self._c_info.tdc_channel_count

    @property
    def pulse_channel_count(self):
         """Channels driven by the readout's pulse generator."""
         return self._c_info.pulse_channel_count

    @property
    def supported(self):
         """Whether this library can drive this readout. Also how a caller
         learns the readout never answered: constructing a Device succeeds
         either way, the enumeration inside it being best-effort."""
         return self._c_info.supported


cdef class Device:
    """An open readout. Follows the contract io.IOBase sets: close() is
    idempotent, closed says whether it has run, and the object is a context
    manager, so

        with Device(address) as dev:
            ...

    releases the readout however the block is left."""

    cdef cdevice.katherine_device_t* _c_device
    # Acquisitions still alive on this device. The C library registers one
    # only at begin(), but katherine_acquisition_init() already stores the
    # device pointer and katherine_acquisition_fini() dereferences it, so
    # closing underneath an acquisition that has merely been initialized
    # would leave that pointer dangling. Counted here because the C guard
    # does not cover it.
    cdef int _acquisitions

    def __cinit__(self, address, uint32_t flags=0):
         # Encode before allocating, for the reason Acquisition.__cinit__
         # gives: an address that is not a string raises here, and
         # __dealloc__ would otherwise finalize a device that was never
         # initialized.
         cdef bytes addr = address.encode()

         self._c_device = <cdevice.katherine_device_t*> PyMem_Malloc(sizeof(cdevice.katherine_device_t))
         if self._c_device is NULL:
             raise MemoryError()

         res = cdevice.katherine_device_init(self._c_device, addr, flags)
         if res != 0:
             # Nothing was opened, so there is nothing to finalize -- but the
             # allocation has to go, or __dealloc__ would finalize a device
             # that never came up.
             PyMem_Free(self._c_device)
             self._c_device = NULL
             check_return_code(res)

    # Every method reaches the C device through this rather than through the
    # field, so that use after close is one check in one place instead of
    # forty-nine, and a method added later cannot forget it.
    cdef cdevice.katherine_device_t* _dev(self) except NULL:
         if self._c_device is NULL:
             raise ValueError('operation on a closed Device')
         return self._c_device

    @property
    def closed(self):
         """True once close() has run, or if construction failed. The
         negative sense is io's, and it makes a half-constructed object read
         as closed rather than as one missing an attribute."""
         return self._c_device is NULL

    def close(self):
         """Releases the readout. Idempotent, and what __exit__ calls.

         Raises ValueError while an Acquisition is still open on this
         device: the C library refuses to finalize one in that state and
         returns without freeing, so closing anyway would free memory it
         still holds. Close the acquisition first; nothing cascades."""
         if self._c_device is NULL:
             return
         if self._acquisitions > 0 or self._c_device.acquisition is not NULL:
             raise ValueError('cannot close a Device while an Acquisition is open on it')
         cdevice.katherine_device_fini(self._c_device)
         PyMem_Free(self._c_device)
         self._c_device = NULL

    def __enter__(self):
         self._dev()
         return self

    def __exit__(self, exc_type, exc_value, traceback):
         self.close()
         return False

    def __dealloc__(self):
         if self._c_device is not NULL:
             # A readout left open holds a claim no other process on this
             # machine can take, so this is a leak worth a warning rather
             # than a quiet cleanup. Suppressed during interpreter shutdown,
             # where warnings machinery may already be gone.
             try:
                 _warnings.warn('unclosed Device', ResourceWarning, stacklevel=2)
             except Exception:
                 pass
             cdevice.katherine_device_fini(self._c_device)
             PyMem_Free(self._c_device)
             self._c_device = NULL


    def set_strict_ack(self, bint strict):
         ctransport.katherine_transport_set_strict_ack(&self._dev().control_transport, strict)

    @property
    def stray_command_responses(self):
         return self._dev().control_transport.stray_command_responses

    @property
    def last_os_error(self):
         """The OS-level code behind the control link's last failure.

         What every error description in the C library means by "see
         katherine_transport_get_last_os_error()": an errno on POSIX, a
         WSAGetLastError() value on Windows, a libusb code over USB.
         Meaningful only directly after a call that failed, and 0 where
         nothing has."""
         return ctransport.katherine_transport_get_last_os_error(&self._dev().control_transport)

    def set_timeout(self, uint32_t timeout_ms):
         """Set how long a control exchange waits for the readout.

         Every command of this library is bounded by it, so shortening it
         makes a readout that is not there fail sooner and a slow one fail
         wrongly."""
         check_return_code(ctransport.katherine_transport_set_timeout(
             &self._dev().control_transport, timeout_ms))

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cdevice.katherine_device_snprint, self._dev())

    def can_correct_timestamp_phase(self):
         return cdevice.katherine_device_can_correct_timestamp_phase(self._dev())

    def get_readout_status(self):
         cdef ReadoutStatus status = ReadoutStatus()
         res = cstatus.katherine_get_readout_status(self._dev(), &status._c_status)
         check_return_code(res)
         return status

    def get_comm_status(self):
         cdef CommStatus status = CommStatus()
         res = cstatus.katherine_get_comm_status(self._dev(), &status._c_status)
         check_return_code(res)
         return status

    def enumerate(self):
         """Ask the readout what it is, filling in what the generation-dependent
         calls need.

         katherine.Device() does this on construction, but a readout that was
         not yet listening then leaves it undone -- and an un-enumerated device
         refuses those calls rather than guessing a generation. Call this once
         the readout is demonstrably answering.
         """
         res = cdevice.katherine_device_enumerate(self._dev())
         check_return_code(res)

    def declare(self, DeviceInfo info):
         """Take the caller's word for what this readout is, instead of
         asking it.

         The only way to run the generation-dependent calls on a device
         opened with DeviceFlags.DONT_ENUMERATE, and the only way to drive a
         readout that does not answer the enumeration at all."""
         res = cdevice.katherine_device_declare(self._dev(), &info._c_info)
         check_return_code(res)

    def select_interface(self):
         """Tell the readout which interface drives it.

         katherine.Device() does this on construction unless
         DeviceFlags.DONT_SELECT_INTERFACE was given. A readout that wants it
         and has not been told answers every later command orders of
         magnitude more slowly, and will not start an acquisition over UDP at
         all."""
         res = cdevice.katherine_device_select_interface(self._dev())
         check_return_code(res)

    def get_chip_id(self, uint8_t chip_index):
         """Ask one chip of the readout for its own identifier."""
         cdef char[:] chip_id = _array.array('b', [0] * cstatus.KATHERINE_CHIP_ID_STR_SIZE)
         cdef char *c_chip_id = &chip_id[0]
         res = cstatus.katherine_get_chip_id(self._dev(), chip_index, c_chip_id)
         check_return_code(res)
         
         if chip_id[0] == 0:
            return None

         return c_chip_id.decode('UTF-8')

    def get_readout_temperature(self):
         cdef float temp
         res = cstatus.katherine_get_readout_temperature(self._dev(), &temp)
         check_return_code(res)
         return temp

    def get_sensor_temperature(self, uint8_t chip_index):
         """Measure one chip's own on-die temperature sensor."""
         cdef float temp
         res = cstatus.katherine_get_sensor_temperature(self._dev(), chip_index, &temp)
         check_return_code(res)
         return temp

    def perform_digital_test(self, uint8_t chip_index):
         """Test the link between the readout and one of its chips."""
         res = cstatus.katherine_tpx3_perform_digital_test(self._dev(), chip_index)
         check_return_code(res)

    def get_adc_voltage(self, unsigned char channel_id):
         cdef float voltage
         res = cstatus.katherine_get_adc_voltage(self._dev(), channel_id, &voltage)
         check_return_code(res)
         return voltage

    def set_test_pulses(self, Tpx3TestPulseConfig test_pulse_config):
         res = cconfig.katherine_tpx3_set_test_pulses(self._dev(), &test_pulse_config._c_test_pulse_config)
         check_return_code(res)

    def configure(self, Config config):
         res = cconfig.katherine_tpx3_configure(self._dev(), config._c_config)
         check_return_code(res)

    def set_all_pixel_config(self, uint8_t chip_index, Tpx3PxConfig px_config):
         """Upload a whole pixel matrix to one chip.

         chip_index trails the matrix here, rather than leading it as it does
         in C, so that callers written against 1.x keep working."""
         res = cconfig.katherine_tpx3_set_all_pixel_config(self._dev(), chip_index, &px_config._c_px_config)
         check_return_code(res)

    def set_acq_time(self, double ns):
         res = cconfig.katherine_set_acq_time(self._dev(), ns)
         check_return_code(res)

    def verify_config(self, uint8_t chip_index):
         """Ask one chip what it kept of the configuration it was given.

         Second generation only."""
         cdef Tpx3ConfigCheck check = Tpx3ConfigCheck()
         res = cconfig.katherine_tpx3_verify_config(self._dev(), chip_index, &check._c_check)
         check_return_code(res)
         return check

    def set_token_count(self, uint8_t token_count):
         """Set how many tokens the readout gives the chip per shutter.

         Deliberately not part of configure(), which leaves whatever the
         readout holds: this is the only way to choose it."""
         res = cconfig.katherine_tpx3_set_token_count(self._dev(), token_count)
         check_return_code(res)

    def set_acq_mode(self, px_mode, bool fast_vco_enabled):
         res = cconfig.katherine_tpx3_set_acq_mode(self._dev(), px_mode.value, fast_vco_enabled)
         check_return_code(res)

    def set_frame_count(self, uint32_t frame_count):
         res = cconfig.katherine_set_frame_count(self._dev(), frame_count)
         check_return_code(res)

    def set_bias(self, unsigned char bias_index, float bias_value):
         res = cconfig.katherine_set_bias(self._dev(), bias_index, bias_value)
         check_return_code(res)

    def set_seq_readout_start(self, int arg):
         res = cconfig.katherine_tpx3_set_seq_readout_start(self._dev(), arg)
         check_return_code(res)

    def set_acquisition_triggers(self, Trigger start_trigger, Trigger stop_trigger):
         res = cconfig.katherine_set_acquisition_triggers(self._dev(), &start_trigger._c_trigger, &stop_trigger._c_trigger)
         check_return_code(res)

    def set_sensor_register(self, uint8_t chip_index, reg_idx, int32_t reg_value):
         """Write one sensor register into the readout's register image.

         The write reaches the sensor only once update_sensor_registers()
         flushes the image.

         chip_index trails the values here, rather than leading them as it
         does in C, so that callers written against 1.x keep working."""
         res = cconfig.katherine_tpx3_set_sensor_register(self._dev(), chip_index, reg_idx.value, reg_value)
         check_return_code(res)

    def update_sensor_registers(self, uint8_t chip_index):
         """Carry one chip's register image to its sensor."""
         res = cconfig.katherine_tpx3_update_sensor_registers(self._dev(), chip_index)
         check_return_code(res)

    def output_block_config_update(self, uint8_t chip_index):
         """Latch one chip's output block configuration."""
         res = cconfig.katherine_tpx3_output_block_config_update(self._dev(), chip_index)
         check_return_code(res)

    def timer_set(self, uint8_t chip_index):
         """Set one chip's timer."""
         res = cconfig.katherine_tpx3_timer_set(self._dev(), chip_index)
         check_return_code(res)

    @property
    def info(self):
         """What this readout said it is, or what a caller declared on its
         behalf. Zeroed until one of the two has happened."""
         cdef DeviceInfo info = DeviceInfo()
         info._c_info = self._dev().info
         return info

    @property
    def derived_info(self):
         """What this readout is, as recognized from its hardware type."""
         cdef DeviceDerivedInfo info = DeviceDerivedInfo()
         info._c_info = self._dev().derived_info
         return info

    def set_dacs(self, uint8_t chip_index, Tpx3Dacs dacs):
         """Write all eighteen DACs and latch them into the chip.

         chip_index trails the values here, rather than leading them as it
         does in C, so that callers written against 1.x keep working."""
         res = cconfig.katherine_tpx3_set_dacs(self._dev(), chip_index, &dacs._c_dacs)
         check_return_code(res)

    def set_dac(self, uint8_t chip_index, dac, uint16_t value):
         """Write one DAC and latch it into the chip.

         The other seventeen keep whatever the readout last held, which is
         its own memory and is lost on a power cycle; set_dacs() is how a
         caller establishes a baseline it knows."""
         res = cconfig.katherine_tpx3_set_dac(self._dev(), chip_index, dac.value, value)
         check_return_code(res)

    def get_bias(self, unsigned char bias_index):
         cdef float voltage
         res = cstatus.katherine_get_bias(self._dev(), bias_index, &voltage)
         check_return_code(res)
         return voltage

    def get_bias_leakage(self, unsigned char bias_index):
         """Volts across the sense resistor, not amperes: the conversion needs
         a per-unit zero, which is the caller's to calibrate."""
         cdef float voltage
         res = cstatus.katherine_get_bias_leakage(self._dev(), bias_index, &voltage)
         check_return_code(res)
         return voltage

    def get_dac_monitor_voltage(self, uint8_t chip_index, monitor):
         cdef float voltage
         res = cstatus.katherine_tpx3_get_dac_monitor_voltage(self._dev(), chip_index, monitor.value, &voltage)
         check_return_code(res)
         return voltage

    def get_dac_monitor_voltages(self, uint8_t chip_index):
         cdef Tpx3DacVoltages voltages = Tpx3DacVoltages()
         res = cstatus.katherine_tpx3_get_dac_monitor_voltages(self._dev(), chip_index, &voltages._c_voltages)
         check_return_code(res)
         return voltages

    def set_gpio_config(self, channels):
         """Takes a sequence of GpioChannelConfig, one per channel."""
         cdef cgpio.katherine_gpio_config_t config
         _fill_gpio_config(&config, channels)
         res = cgpio.katherine_set_gpio_config(self._dev(), &config)
         check_return_code(res)

    def set_composed_ready_signal(self, channels):
         cdef cgpio.katherine_gpio_config_t config
         _fill_gpio_config(&config, channels)
         res = cgpio.katherine_set_composed_ready_signal(self._dev(), &config)
         check_return_code(res)

    def probe_gpio(self):
         cdef GpioMonitor state = GpioMonitor()
         res = cgpio.katherine_probe_gpio(self._dev(), &state._c_state)
         check_return_code(res)
         return state

    def set_tdc_config(self, channels):
         """Takes a sequence of TdcChannelConfig, one per channel."""
         cdef ctdc.katherine_tdc_config_t config
         _fill_tdc_config(&config, channels)
         res = ctdc.katherine_set_tdc_config(self._dev(), &config)
         check_return_code(res)

    def get_tdc_counts(self):
         cdef ctdc.katherine_tdc_counts_t counts
         res = ctdc.katherine_get_tdc_counts(self._dev(), &counts)
         check_return_code(res)
         return [counts.channel[i] for i in range(ctdc.KATHERINE_MAX_TDC_CHANNEL_COUNT)]

    def set_pulse_generator(self, PulseConfig config):
         res = cpulse.katherine_set_pulse_generator(self._dev(), &config._c_config)
         check_return_code(res)

    def get_pulse_generator_counts(self):
         cdef PulseCounts counts = PulseCounts()
         res = cpulse.katherine_get_pulse_generator_counts(self._dev(), &counts._c_counts)
         check_return_code(res)
         return counts

    def reset_pulse_generator_counts(self):
         res = cpulse.katherine_reset_pulse_generator_counts(self._dev())
         check_return_code(res)


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
    def chip_count(self):
       return self._c_status.chip_count


cdef class Tpx3ConfigCheck:
    """What a chip kept of the configuration it was given.

    Both flags are the chip's own read-back, not this library's."""
    cdef cconfig.katherine_tpx3_config_check_t _c_check

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cconfig.katherine_tpx3_config_check_snprint, &self._c_check)

    @property
    def matrix_ok(self):
       """True if the pixel matrix was read back without a parity error."""
       return self._c_check.matrix_ok

    @property
    def dacs_ok(self):
       """True if every DAC was read back as written."""
       return self._c_check.dacs_ok


cdef class Trigger:
    cdef cconfig.katherine_trigger_t _c_trigger

    def __init__(self, enabled=False, channel=0, edge=None, delayed=False, delay_seconds=0.0, cdata=None):
         if cdata is None:
             self._c_trigger.enabled = enabled
             self._c_trigger.channel = TriggerSource(channel).value
             self._c_trigger.edge = TriggerEdge.RISING.value if edge is None else TriggerEdge(edge).value
             self._c_trigger.delayed = delayed
             self._c_trigger.delay_seconds = delay_seconds
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
         return TriggerSource(self._c_trigger.channel)

    @channel.setter
    def channel(self, val):
         self._c_trigger.channel = TriggerSource(val).value

    @property
    def edge(self):
         return TriggerEdge(self._c_trigger.edge)

    @edge.setter
    def edge(self, val):
         self._c_trigger.edge = TriggerEdge(val).value

    @property
    def delayed(self):
         return self._c_trigger.delayed

    @delayed.setter
    def delayed(self, val):
         self._c_trigger.delayed = val

    @property
    def delay_seconds(self):
         return self._c_trigger.delay_seconds

    @delay_seconds.setter
    def delay_seconds(self, val):
         self._c_trigger.delay_seconds = val


cdef class Tpx3TestPulseConfig:
    cdef cconfig.katherine_tpx3_test_pulse_config_t _c_test_pulse_config

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
         return _snprint_repr(<snprint_fn_t> cconfig.katherine_tpx3_test_pulse_config_snprint, &self._c_test_pulse_config)

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


cdef class Tpx3Dacs:
    cdef cconfig.katherine_tpx3_dacs_t _c_dacs

    def __init__(self, cdata=None):
         if cdata is not None:
             self._c_dacs.named = cdata['named']

    def validate(self):
         """Check every DAC against its own width, Timepix3 manual Table 11.

         Opt-in, as in C: Device.set_dacs() transmits what it is given, and a
         value too wide for its DAC is truncated by the chip rather than
         refused."""
         check_return_code(cconfig.katherine_tpx3_dacs_validate(&self._c_dacs))

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cconfig.katherine_tpx3_dacs_snprint, &self._c_dacs)

    def __len__(self):
         return TPX3_DAC_COUNT

    def __getitem__(self, int dac):
         if dac < 0 or dac >= TPX3_DAC_COUNT:
             raise IndexError('DAC out of range')
         return self._c_dacs.array[dac]

    def __setitem__(self, int dac, uint16_t value):
         if dac < 0 or dac >= TPX3_DAC_COUNT:
             raise IndexError('DAC out of range')
         self._c_dacs.array[dac] = value

    def name(self, int dac):
         if dac < 0 or dac >= TPX3_DAC_COUNT:
             raise IndexError('DAC out of range')
         return cconfig.katherine_tpx3_dac_name(<cconfig.katherine_tpx3_dac_t> dac).decode('UTF-8')

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
    def Vpreamp_NCAS(self):
       return self._c_dacs.named.Vpreamp_NCAS

    @Vpreamp_NCAS.setter
    def Vpreamp_NCAS(self, val):
       self._c_dacs.named.Vpreamp_NCAS = val

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


cdef class Tpx3PxConfig:
    cdef cpx_config.katherine_tpx3_px_config_t _c_px_config

    def __init__(self, cdata=None):
      if cdata is not None:
         self._c_px_config = cdata

    def __repr__(self):
      return _snprint_repr(<snprint_fn_t> cpx_config.katherine_tpx3_px_config_snprint, &self._c_px_config)

    @staticmethod
    def from_bmc(path):
      cdef cpx_config.katherine_tpx3_px_config_t config
      res = cpx_config.katherine_tpx3_px_config_load_bmc_file(&config, path.encode())
      check_return_code(res)
      return Tpx3PxConfig(cdata=config)

    @staticmethod
    def from_bpc(path):
      cdef cpx_config.katherine_tpx3_px_config_t config
      res = cpx_config.katherine_tpx3_px_config_load_bpc_file(&config, path.encode())
      check_return_code(res)
      return Tpx3PxConfig(cdata=config)

    @staticmethod
    def from_bmc_data(data):
      cdef const unsigned char[::1] view = data
      if view.shape[0] != sizeof(cpx_config.katherine_tpx3_bmc_t):
         raise ValueError('BMC data must be exactly %d bytes long' % sizeof(cpx_config.katherine_tpx3_bmc_t))
      cdef cpx_config.katherine_tpx3_px_config_t config
      res = cpx_config.katherine_tpx3_px_config_load_bmc_data(&config, <const cpx_config.katherine_tpx3_bmc_t *> &view[0])
      check_return_code(res)
      return Tpx3PxConfig(cdata=config)

    @staticmethod
    def from_bpc_data(data):
      cdef const unsigned char[::1] view = data
      if view.shape[0] != sizeof(cpx_config.katherine_tpx3_bpc_t):
         raise ValueError('BPC data must be exactly %d bytes long' % sizeof(cpx_config.katherine_tpx3_bpc_t))
      cdef cpx_config.katherine_tpx3_px_config_t config
      res = cpx_config.katherine_tpx3_px_config_load_bpc_data(&config, <const cpx_config.katherine_tpx3_bpc_t *> &view[0])
      check_return_code(res)
      return Tpx3PxConfig(cdata=config)

    @staticmethod
    cdef cpx.katherine_tpx3_coord_t _coord(int x, int y) except *:
      if not (0 <= x <= 255 and 0 <= y <= 255):
         raise ValueError('pixel coordinates must lie within 0 to 255')

      cdef cpx.katherine_tpx3_coord_t coord
      coord.x = x
      coord.y = y
      return coord

    def set_test_bit(self, int x, int y, bool enabled):
      cpx_config.katherine_tpx3_px_config_set_test_bit(&self._c_px_config, Tpx3PxConfig._coord(x, y), enabled)

    def get_test_bit(self, int x, int y):
      return cpx_config.katherine_tpx3_px_config_get_test_bit(&self._c_px_config, Tpx3PxConfig._coord(x, y))

    def set_mask_bit(self, int x, int y, bool masked):
      cpx_config.katherine_tpx3_px_config_set_mask_bit(&self._c_px_config, Tpx3PxConfig._coord(x, y), masked)

    def get_mask_bit(self, int x, int y):
      return cpx_config.katherine_tpx3_px_config_get_mask_bit(&self._c_px_config, Tpx3PxConfig._coord(x, y))

    def set_loc_thl(self, int x, int y, uint8_t loc_thl):
      if loc_thl > 15:
         raise ValueError('local threshold adjustment must lie within 0 to 15')
      cpx_config.katherine_tpx3_px_config_set_loc_thl(&self._c_px_config, Tpx3PxConfig._coord(x, y), loc_thl)

    def get_loc_thl(self, int x, int y):
      return cpx_config.katherine_tpx3_px_config_get_loc_thl(&self._c_px_config, Tpx3PxConfig._coord(x, y))

@_unique
class TriggerEdge(_Enum):
    RISING  = cconfig.katherine_trigger_edge_t.KATHERINE_TRIGGER_EDGE_RISING
    FALLING = cconfig.katherine_trigger_edge_t.KATHERINE_TRIGGER_EDGE_FALLING

    def __str__(self):
        """Render as the C library does, e.g. 'rising'."""
        return cconfig.katherine_str_trigger_edge(self.value).decode('UTF-8')


@_unique
class TriggerSource(_Enum):
    """What a trigger listens to. Not TdcSource's numbering: zero is the
    pulse generator's internal channel, so GPIO channel 0 is one."""
    PULSE_INTERNAL = cconfig.katherine_trigger_source_t.KATHERINE_TRIGGER_SOURCE_PULSE_INTERNAL
    GPIO_0         = cconfig.katherine_trigger_source_t.KATHERINE_TRIGGER_SOURCE_GPIO_0
    GPIO_1         = cconfig.katherine_trigger_source_t.KATHERINE_TRIGGER_SOURCE_GPIO_1
    GPIO_2         = cconfig.katherine_trigger_source_t.KATHERINE_TRIGGER_SOURCE_GPIO_2
    GPIO_3         = cconfig.katherine_trigger_source_t.KATHERINE_TRIGGER_SOURCE_GPIO_3
    GPIO_4         = cconfig.katherine_trigger_source_t.KATHERINE_TRIGGER_SOURCE_GPIO_4
    GPIO_5         = cconfig.katherine_trigger_source_t.KATHERINE_TRIGGER_SOURCE_GPIO_5
    GPIO_6         = cconfig.katherine_trigger_source_t.KATHERINE_TRIGGER_SOURCE_GPIO_6
    GPIO_7         = cconfig.katherine_trigger_source_t.KATHERINE_TRIGGER_SOURCE_GPIO_7


@_unique
class Polarity(_Enum):
    HOLES     = cconfig.katherine_polarity_t.KATHERINE_POLARITY_HOLES
    ELECTRONS = cconfig.katherine_polarity_t.KATHERINE_POLARITY_ELECTRONS

    def __str__(self):
        """Render as the C library does, e.g. 'holes'."""
        return cconfig.katherine_str_polarity(self.value).decode('UTF-8')


@_unique
class Tpx3Phase(_Enum):
    PHASE_1          = cconfig.katherine_tpx3_phase_t.KATHERINE_TPX3_PHASE_1
    PHASE_2          = cconfig.katherine_tpx3_phase_t.KATHERINE_TPX3_PHASE_2
    PHASE_4          = cconfig.katherine_tpx3_phase_t.KATHERINE_TPX3_PHASE_4
    PHASE_8          = cconfig.katherine_tpx3_phase_t.KATHERINE_TPX3_PHASE_8
    PHASE_16         = cconfig.katherine_tpx3_phase_t.KATHERINE_TPX3_PHASE_16

    def __str__(self):
        """Render as the C library does, e.g. 'phase_1'."""
        return cconfig.katherine_str_phase(self.value).decode('UTF-8')


@_unique
class Tpx3Freq(_Enum):
    FREQ_20          = cconfig.katherine_tpx3_freq_t.KATHERINE_TPX3_FREQ_20_MHZ
    FREQ_40          = cconfig.katherine_tpx3_freq_t.KATHERINE_TPX3_FREQ_40_MHZ
    FREQ_80          = cconfig.katherine_tpx3_freq_t.KATHERINE_TPX3_FREQ_80_MHZ
    FREQ_160         = cconfig.katherine_tpx3_freq_t.KATHERINE_TPX3_FREQ_160_MHZ

    def __str__(self):
        """Render as the C library does, e.g. 'freq_40'."""
        return cconfig.katherine_str_freq(self.value).decode('UTF-8')



def tpx3_actual_phases(freq, phase):
    """Phases the clock divider actually grants, which is not what the
    enumerator asks for: Timepix3 manual Table 17 clamps the count at the
    faster frequencies."""
    return cconfig.katherine_tpx3_actual_phases(freq.value, phase.value)


def tpx3_freq_is_fast_vco_supported(freq):
    """True where the fine oscillator can run, which only the slowest pixel
    clock allows."""
    return cconfig.katherine_tpx3_freq_is_fast_vco_supported(freq.value)


def tpx3_toa_coarse_tick_to_fine_ticks(freq):
    return ctoa.katherine_tpx3_toa_coarse_tick_to_fine_ticks(freq.value)


def tpx3_toa_coarse_tick_to_fine_shift(freq):
    return ctoa.katherine_tpx3_toa_coarse_tick_to_fine_shift(freq.value)


def tpx3_toa_epoch_bias(uint8_t coarse_tick_to_fine_shift):
    return ctoa.katherine_tpx3_toa_epoch_bias(coarse_tick_to_fine_shift)


def tpx3_timestamp_to_seconds(uint64_t timestamp):
    cdef uint64_t sec
    cdef double nsec
    ctoa.katherine_tpx3_timestamp_to_seconds(timestamp, &sec, &nsec)
    return (sec, nsec)


def tpx3_timestamp_to_toa_ftoa(uint8_t coarse_tick_to_fine_shift, uint8_t phase_offset, uint64_t timestamp):
    cdef uint64_t toa
    cdef uint8_t ftoa
    ctoa.katherine_tpx3_timestamp_to_toa_ftoa(coarse_tick_to_fine_shift, phase_offset, timestamp, &toa, &ftoa)
    return (toa, ftoa)


# The one enumeration here without @_unique, because the C one it mirrors
# carries a deliberate alias: register 0 is named TEST_PULSE_METHOD in the
# 1.x spelling and TEST_PULSE_PERIOD in the Tpx3 manual's, which is the
# correct one. @_unique exists to catch a transcription that collapsed two
# registers onto one value; here the collision is the point, so the guard
# is dropped on this enum alone rather than the alias on the name.
class Tpx3Reg(_Enum):
    TEST_PULSE_METHOD     = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_TEST_PULSE_METHOD
    #: The same register under the name the Tpx3 manual gives it: Table 9
    #: header 0h0C carries TP_period in bits 7:0 and TP_phase in 11:8, not a
    #: single "method".
    TEST_PULSE_PERIOD     = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_TEST_PULSE_PERIOD
    NUMBER_TEST_PULSES    = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_NUMBER_TEST_PULSES
    OUT_BLOCK_CONFIG      = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_OUT_BLOCK_CONFIG
    PLL_CONFIG            = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_PLL_CONFIG
    GENERAL_CONFIG        = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_GENERAL_CONFIG
    SLVS_CONFIG           = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_SLVS_CONFIG
    POWER_PULSING_PATTERN = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_POWER_PULSING_PATTERN
    SET_TIMER_LOW         = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_SET_TIMER_LOW
    SET_TIMER_MID         = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_SET_TIMER_MID
    SET_TIMER_HIGH        = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_SET_TIMER_HIGH
    SENSE_DAC_SELECTOR    = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_SENSE_DAC_SELECTOR
    EXT_DAC_SELECTOR      = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_EXT_DAC_SELECTOR


cdef class Config:
    cdef cconfig.katherine_tpx3_config_t *_c_config
    cdef readonly uint8_t chip_count

    def __cinit__(self, chip_count=1):
         if chip_count < 1 or chip_count > 255:
             raise ValueError('chip_count must be between 1 and 255')
         # The per-chip settings are a flexible array in C, so the size is
         # not known until the chip count is. calloc, not malloc: zero is a
         # safe value for every field, Polarity.HOLES included.
         self._c_config = <cconfig.katherine_tpx3_config_t *> calloc(
             1, sizeof(cconfig.katherine_tpx3_config_t) + chip_count * sizeof(cconfig.katherine_tpx3_chip_config_t))
         if self._c_config is NULL:
             raise MemoryError('cannot allocate a configuration for %d chips' % chip_count)
         self.chip_count = chip_count
         self._c_config.common.chip_type = cdevice.KATHERINE_CHIP_TPX3
         self._c_config.common.chip_count = chip_count

    def __dealloc__(self):
         free(self._c_config)
         self._c_config = NULL

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cconfig.katherine_tpx3_config_snprint, self._c_config)

    cdef _check_chip(self, chip_index):
         if chip_index < 0 or chip_index >= self.chip_count:
             raise IndexError('chip %d of %d' % (chip_index, self.chip_count))

    @property
    def acq_time(self):
       return self._c_config.common.acq_time

    @acq_time.setter
    def acq_time(self, val):
       self._c_config.common.acq_time = val

    @property
    def frame_count(self):
       return self._c_config.common.frame_count

    @frame_count.setter
    def frame_count(self, val):
       self._c_config.common.frame_count = val

    @property
    def bias(self):
       """Bias voltage asked of each supply, in volts, addressed from 0. A
       copy of the array, so assign the property to change it."""
       return [self._c_config.common.bias[i] for i in range(MAX_BIAS_SUPPLIES)]

    @bias.setter
    def bias(self, val):
       if len(val) > MAX_BIAS_SUPPLIES:
           raise ValueError('at most %d bias supplies' % MAX_BIAS_SUPPLIES)
       for i in range(MAX_BIAS_SUPPLIES):
           self._c_config.common.bias[i] = val[i] if i < len(val) else 0.0

    @property
    def start_trigger(self):
       return Trigger(cdata=self._c_config.common.start_trigger)

    cdef _set_start_trigger(self, Trigger val):
         memcpy(&self._c_config.common.start_trigger, &val._c_trigger, sizeof(self._c_config.common.start_trigger))

    @start_trigger.setter
    def start_trigger(self, val):
         self._set_start_trigger(val)

    @property
    def stop_trigger(self):
       return Trigger(cdata=self._c_config.common.stop_trigger)

    cdef _set_stop_trigger(self, Trigger val):
         memcpy(&self._c_config.common.stop_trigger, &val._c_trigger, sizeof(self._c_config.common.stop_trigger))

    @stop_trigger.setter
    def stop_trigger(self, val):
         self._set_stop_trigger(val)

    def gray_disable(self, chip_index):
       """True if one chip's timestamps stay in its Gray code."""
       self._check_chip(chip_index)
       return self._c_config.chips[chip_index].gray_disable

    def set_gray_disable(self, chip_index, val):
       """Sets whether one chip's timestamps stay in Gray code."""
       self._check_chip(chip_index)
       self._c_config.chips[chip_index].gray_disable = val

    def polarity(self, chip_index):
       """Carriers one chip's sensor collects."""
       self._check_chip(chip_index)
       return Polarity(self._c_config.chips[chip_index].polarity)

    def set_polarity(self, chip_index, val):
       """Sets the carriers one chip's sensor collects."""
       self._check_chip(chip_index)
       self._c_config.chips[chip_index].polarity = Polarity(val).value

    # A request only: what actually happens depends on the device and on the
    # phase count, and is reported afterwards by Acquisition.phase_correction,
    # not by this property.
    @property
    def correct_clock_phase(self):
       return self._c_config.correct_clock_phase

    @correct_clock_phase.setter
    def correct_clock_phase(self, val):
       self._c_config.correct_clock_phase = val

    @property
    def clock_phase(self):
       return Tpx3Phase(self._c_config.clock_phase)

    @clock_phase.setter
    def clock_phase(self, val):
       self._c_config.clock_phase = val.value

    @property
    def clock_frequency(self):
       return Tpx3Freq(self._c_config.clock_frequency)

    @clock_frequency.setter
    def clock_frequency(self, val):
       self._c_config.clock_frequency = val.value

    def dacs(self, chip_index):
       """One chip's eighteen DACs."""
       self._check_chip(chip_index)
       return Tpx3Dacs(cdata=self._c_config.chips[chip_index].dacs)

    def set_dacs(self, chip_index, Tpx3Dacs val):
       """Sets one chip's eighteen DACs."""
       self._check_chip(chip_index)
       memcpy(&self._c_config.chips[chip_index].dacs, &val._c_dacs, sizeof(val._c_dacs))

    def pixels(self, chip_index):
       """One chip's pixel matrix."""
       self._check_chip(chip_index)
       return Tpx3PxConfig(cdata=self._c_config.chips[chip_index].pixels)

    def set_pixels(self, chip_index, Tpx3PxConfig val):
       """Sets one chip's pixel matrix."""
       self._check_chip(chip_index)
       memcpy(&self._c_config.chips[chip_index].pixels, &val._c_px_config, sizeof(val._c_px_config))

    @property
    def readout_mode(self):
       return Tpx3ReadoutMode(self._c_config.readout_mode)

    @readout_mode.setter
    def readout_mode(self, val):
       self._c_config.readout_mode = val.value

    @property
    def px_mode(self):
       return Tpx3PxMode(self._c_config.px_mode)

    @px_mode.setter
    def px_mode(self, val):
       self._c_config.px_mode = val.value

    @property
    def fast_vco_enable(self):
       return self._c_config.fast_vco_enable

    @fast_vco_enable.setter
    def fast_vco_enable(self, val):
       self._c_config.fast_vco_enable = val

    @property
    def test_pulses(self):
       return Tpx3TestPulseConfig(cdata=self._c_config.test_pulses)

    cdef _set_test_pulses(self, Tpx3TestPulseConfig val):
         memcpy(&self._c_config.test_pulses, &val._c_test_pulse_config, sizeof(self._c_config.test_pulses))

    @test_pulses.setter
    def test_pulses(self, val):
         self._set_test_pulses(val)


@_unique
class Tpx3PxMode(_Enum):
    TOA_TOT          = cconfig.katherine_tpx3_px_mode_t.KATHERINE_TPX3_PX_TOA_TOT
    ONLY_TOA         = cconfig.katherine_tpx3_px_mode_t.KATHERINE_TPX3_PX_ONLY_TOA
    EVENT_COUNT_ITOT = cconfig.katherine_tpx3_px_mode_t.KATHERINE_TPX3_PX_EVENT_COUNT_ITOT

    def __str__(self):
        """Render as the C library does, e.g. 'toa_tot'."""
        return cconfig.katherine_str_px_mode(self.value).decode('UTF-8')


@_unique
class AcquisitionState(_Enum):
    NOT_STARTED      = cacquisition.katherine_acquisition_state_t.KATHERINE_ACQUISITION_STATE_NOT_STARTED
    RUNNING          = cacquisition.katherine_acquisition_state_t.KATHERINE_ACQUISITION_STATE_RUNNING
    SUCCEEDED        = cacquisition.katherine_acquisition_state_t.KATHERINE_ACQUISITION_STATE_SUCCEEDED
    TIMED_OUT        = cacquisition.katherine_acquisition_state_t.KATHERINE_ACQUISITION_STATE_TIMED_OUT

    def __str__(self):
        """Render as the C library does, e.g. 'running'."""
        return cacquisition.katherine_str_acquisition_state(self.value).decode('UTF-8')


@_unique
class Tpx3PhaseCorrection(_Enum):
    NONE     = cacquisition.katherine_tpx3_phase_correction_t.KATHERINE_TPX3_PHASE_CORRECTION_NONE
    SOFTWARE = cacquisition.katherine_tpx3_phase_correction_t.KATHERINE_TPX3_PHASE_CORRECTION_SOFTWARE
    HARDWARE = cacquisition.katherine_tpx3_phase_correction_t.KATHERINE_TPX3_PHASE_CORRECTION_HARDWARE

    def __str__(self):
        """Render as the C library does, e.g. 'hardware'."""
        return cacquisition.katherine_str_phase_correction(self.value).decode('UTF-8')


@_unique
class Tpx3ReadoutMode(_Enum):
    FRAME_BASED         = cacquisition.katherine_tpx3_readout_mode_t.KATHERINE_TPX3_READOUT_SEQUENTIAL
    DATA_DRIVEN         = cacquisition.katherine_tpx3_readout_mode_t.KATHERINE_TPX3_READOUT_DATA_DRIVEN

    def __str__(self):
        """Render as the C library does, e.g. 'sequential'."""
        return cacquisition.katherine_str_readout_mode(self.value).decode('UTF-8')


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


cdef class Tpx3PxFastToaTot:
    cdef cpx.katherine_tpx3_px_f_toa_tot_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cpx.katherine_tpx3_px_f_toa_tot_snprint, &self._c_px)

    @staticmethod
    def RAW_SIZE():
       return sizeof(cpx.katherine_tpx3_px_f_toa_tot_t)

    @property
    def chip(self):
       return self._c_px.chip

    @property
    def x(self):
       return self._c_px.coord.x

    @property
    def y(self):
       return self._c_px.coord.y

    @property
    def timestamp(self):
       return self._c_px.timestamp

    @property
    def tot(self):
       return self._c_px.tot


cdef class Tpx3PxToaTot:
    cdef cpx.katherine_tpx3_px_toa_tot_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cpx.katherine_tpx3_px_toa_tot_snprint, &self._c_px)

    @staticmethod
    def RAW_SIZE():
       return sizeof(cpx.katherine_tpx3_px_toa_tot_t)

    @property
    def chip(self):
       return self._c_px.chip

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
    def timestamp(self):
       return self._c_px.timestamp

    @property
    def tot(self):
       return self._c_px.tot


cdef class Tpx3PxFastToaOnly:
    cdef cpx.katherine_tpx3_px_f_toa_only_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cpx.katherine_tpx3_px_f_toa_only_snprint, &self._c_px)

    @staticmethod
    def RAW_SIZE():
       return sizeof(cpx.katherine_tpx3_px_f_toa_only_t)

    @property
    def chip(self):
       return self._c_px.chip

    @property
    def x(self):
       return self._c_px.coord.x

    @property
    def y(self):
       return self._c_px.coord.y

    @property
    def timestamp(self):
       return self._c_px.timestamp


cdef class Tpx3PxToaOnly:
    cdef cpx.katherine_tpx3_px_toa_only_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cpx.katherine_tpx3_px_toa_only_snprint, &self._c_px)

    @staticmethod
    def RAW_SIZE():
       return sizeof(cpx.katherine_tpx3_px_toa_only_t)

    @property
    def chip(self):
       return self._c_px.chip

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
    def timestamp(self):
       return self._c_px.timestamp


cdef class Tpx3PxFastEventCountItot:
    cdef cpx.katherine_tpx3_px_f_event_count_itot_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cpx.katherine_tpx3_px_f_event_count_itot_snprint, &self._c_px)

    @staticmethod
    def RAW_SIZE():
       return sizeof(cpx.katherine_tpx3_px_f_event_count_itot_t)

    @property
    def chip(self):
       return self._c_px.chip

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


cdef class Tpx3PxEventCountItot:
    cdef cpx.katherine_tpx3_px_event_count_itot_t _c_px

    def __init__(self, cdata=None):
       if cdata is not None:
           self._c_px = cdata

    def __repr__(self):
       return _snprint_repr(<snprint_fn_t> cpx.katherine_tpx3_px_event_count_itot_snprint, &self._c_px)

    @staticmethod
    def RAW_SIZE():
       return sizeof(cpx.katherine_tpx3_px_event_count_itot_t)

    @property
    def chip(self):
       return self._c_px.chip

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


cdef class Acquisition:
    """A measurement on an open readout. Follows the same contract Device
    does: close() is idempotent, closed says whether it has run, and the
    object is a context manager. Nesting unwinds inner-first, which is the
    order the C library requires an acquisition and its device be finalized
    in, so the `with` form cannot get it wrong -- including where the block
    is left by an exception."""

    cdef cacquisition.katherine_acquisition_t* _c_acq
    cdef public AcquisitionObserver observer
    # A strong reference, so the device cannot be collected while this holds
    # a pointer into it.
    cdef Device _device

    def __cinit__(self, Device dev, size_t md_buffer_size, size_t pixel_buffer_size, int report_timeout, int fail_timeout):
      # Ask the device for its pointer first. _dev() refuses a closed one, and
      # __dealloc__ runs even when __cinit__ raises, so anything that can
      # raise has to happen before there is an allocation to finalize.
      cdef cdevice.katherine_device_t* d = dev._dev()

      self._c_acq = <cacquisition.katherine_acquisition_t*> PyMem_Malloc(sizeof(cacquisition.katherine_acquisition_t))
      if self._c_acq is NULL:
          raise MemoryError()

      res = cacquisition.katherine_acquisition_init(self._c_acq, d, <void*> self, md_buffer_size, pixel_buffer_size, report_timeout, fail_timeout)
      if res != 0:
          PyMem_Free(self._c_acq)
          self._c_acq = NULL
          check_return_code(res)

      self._c_acq.handlers.frame_started = _forward_frame_started
      self._c_acq.handlers.frame_ended = _forward_frame_ended
      self._c_acq.handlers.pixels_received = NULL
      self._c_acq.handlers.data_received = _forward_data_received
      self.observer = AcquisitionObserver()
      self._device = dev
      dev._acquisitions += 1

    # As Device._dev(): one check in one place, so a method added later
    # cannot reach a freed acquisition.
    cdef cacquisition.katherine_acquisition_t* _acq(self) except NULL:
      if self._c_acq is NULL:
          raise ValueError('operation on a closed Acquisition')
      return self._c_acq

    @property
    def closed(self):
      """True once close() has run, or if construction failed."""
      return self._c_acq is NULL

    def close(self):
      """Ends the measurement and releases its buffers. Idempotent, and
      what __exit__ calls. Must happen before the Device it runs on is
      closed, which Device.close() enforces."""
      if self._c_acq is NULL:
          return
      cacquisition.katherine_acquisition_fini(self._c_acq)
      PyMem_Free(self._c_acq)
      self._c_acq = NULL
      if self._device is not None:
          self._device._acquisitions -= 1
          self._device = None

    def __enter__(self):
      self._acq()
      return self

    def __exit__(self, exc_type, exc_value, traceback):
      self.close()
      return False

    def __dealloc__(self):
      # The free used to sit outside this guard and never cleared the
      # pointer, so close() and collection would both have freed it.
      if self._c_acq is not NULL:
         try:
             _warnings.warn('unclosed Acquisition', ResourceWarning, stacklevel=2)
         except Exception:
             pass
         cacquisition.katherine_acquisition_fini(self._c_acq)
         PyMem_Free(self._c_acq)
         self._c_acq = NULL
         if self._device is not None:
             self._device._acquisitions -= 1
             self._device = None

    def __repr__(self):
      return _snprint_repr(<snprint_fn_t> cacquisition.katherine_acquisition_snprint, self._acq())

    def begin(self, Config config, bool decode_data=True):
      """Configures the readout and opens the shutter. The readout mode, the
      pixel mode and the oscillator setting come from the configuration,
      where they used to be arguments here."""
      px_mode = config.px_mode
      if config.fast_vco_enable:
        if px_mode == Tpx3PxMode.TOA_TOT:
          self._acq().handlers.pixels_received = _forward_pixels_received_f_toa_tot
        elif px_mode == Tpx3PxMode.ONLY_TOA:
          self._acq().handlers.pixels_received = _forward_pixels_received_f_toa_only
        elif px_mode == Tpx3PxMode.EVENT_COUNT_ITOT:
          self._acq().handlers.pixels_received = _forward_pixels_received_f_event_count_itot
      else:
        if px_mode == Tpx3PxMode.TOA_TOT:
          self._acq().handlers.pixels_received = _forward_pixels_received_toa_tot
        elif px_mode == Tpx3PxMode.ONLY_TOA:
          self._acq().handlers.pixels_received = _forward_pixels_received_toa_only
        elif px_mode == Tpx3PxMode.EVENT_COUNT_ITOT:
          self._acq().handlers.pixels_received = _forward_pixels_received_event_count_itot

      res = cacquisition.katherine_acquisition_begin(self._acq(), config._c_config, decode_data)
      check_return_code(res)

    def abort(self):
      res = cacquisition.katherine_acquisition_abort(self._acq())
      check_return_code(res)

    def stop(self):
      res = cacquisition.katherine_acquisition_stop(self._acq())
      check_return_code(res)
         
    def read(self):
      res = cacquisition.katherine_acquisition_read(self._acq())
      check_return_code(res)

    @property
    def state(self):
       return AcquisitionState(self._acq().state)

    @property
    def aborted(self):
       return self._acq().aborted

    @property
    def requested_frames(self):
       return self._acq().requested_frames

    @property
    def completed_frames(self):
       return self._acq().completed_frames

    @property
    def dropped_measurement_data(self):
       return self._acq().dropped_measurement_data

    @property
    def truncated_measurement_data(self):
       return self._acq().truncated_measurement_data

    @property
    def dropped_input(self):
       """Measurement data the OS dropped for want of room in its receive queue.

       Counted by the kernel, not by this library, and so a different number
       from dropped_measurement_data, which is what the decoder could not
       keep up with. Raises on a link that keeps no such queue, the USB one
       among them."""
       cdef uint64_t count = 0
       check_return_code(ctransport.katherine_transport_get_dropped_input(
           &self._acq().data_transport, &count))
       return count

    @property
    def last_os_error(self):
       """The OS-level code behind the data link's last failure; see
       Device.last_os_error."""
       return ctransport.katherine_transport_get_last_os_error(&self._acq().data_transport)

    def set_timeout(self, uint32_t timeout_ms):
       """Set how long a data read waits before it reports a timeout.

       What report_timeout is measured in steps of, so a report timeout
       shorter than this one is never observed sooner than this one."""
       check_return_code(ctransport.katherine_transport_set_timeout(
           &self._acq().data_transport, timeout_ms))

    @property
    def readout_mode(self):
       return Tpx3ReadoutMode(self._acq().readout_mode)

    @property
    def px_mode(self):
       return Tpx3PxMode(self._acq().px_mode)

    @property
    def fast_vco_enabled(self):
       return self._acq().fast_vco_enabled

    @property
    def decode_data(self):
       return self._acq().decode_data

    @property
    def md_buffer_size(self):
       return self._acq().md_buffer_size

    @property
    def pixel_buffer_size(self):
       return self._acq().pixel_buffer_size

    @property
    def requested_frame_duration(self):
       return self._acq().requested_frame_duration

    @property
    def report_timeout(self):
       return self._acq().report_timeout

    @property
    def fail_timeout(self):
       return self._acq().fail_timeout

    @property
    def phase_correction(self):
       return Tpx3PhaseCorrection(self._acq().phase_correction)

    def timestamp_phase_offset(self, int x, int y):
       return cacquisition.katherine_acquisition_timestamp_phase_offset(self._acq(), Tpx3PxConfig._coord(x, y))


cdef void _forward_frame_started(void *user_ctx, int frame_idx) noexcept:
    (<Acquisition> user_ctx).observer.frame_started(frame_idx)

cdef void _forward_frame_ended(void *user_ctx, int frame_idx, bool completed, const cacquisition.katherine_frame_info_t *info) noexcept:
    py_info = FrameInfo()
    memcpy(&py_info._c_info, info, sizeof(py_info._c_info))
    (<Acquisition> user_ctx).observer.frame_ended(frame_idx, completed, py_info)

cdef void _forward_data_received(void *user_ctx, const char *data, size_t count) noexcept:
    (<Acquisition> user_ctx).observer.data_received(data[:count])

cdef void _forward_pixels_received_f_toa_tot(void *user_ctx, const void *px, size_t count) noexcept:
    cdef const cpx.katherine_tpx3_px_f_toa_tot_t *dpx = <const cpx.katherine_tpx3_px_f_toa_tot_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([Tpx3PxFastToaTot(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_toa_tot(void *user_ctx, const void *px, size_t count) noexcept:
    cdef const cpx.katherine_tpx3_px_toa_tot_t *dpx = <const cpx.katherine_tpx3_px_toa_tot_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([Tpx3PxToaTot(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_f_toa_only(void *user_ctx, const void *px, size_t count) noexcept:
    cdef const cpx.katherine_tpx3_px_f_toa_only_t *dpx = <const cpx.katherine_tpx3_px_f_toa_only_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([Tpx3PxFastToaOnly(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_toa_only(void *user_ctx, const void *px, size_t count) noexcept:
    cdef const cpx.katherine_tpx3_px_toa_only_t *dpx = <const cpx.katherine_tpx3_px_toa_only_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([Tpx3PxToaOnly(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_f_event_count_itot(void *user_ctx, const void *px, size_t count) noexcept:
    cdef const cpx.katherine_tpx3_px_f_event_count_itot_t *dpx = <const cpx.katherine_tpx3_px_f_event_count_itot_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([Tpx3PxFastEventCountItot(cdata=dpx[i]) for i in range(count)])

cdef void _forward_pixels_received_event_count_itot(void *user_ctx, const void *px, size_t count) noexcept:
    cdef const cpx.katherine_tpx3_px_event_count_itot_t *dpx = <const cpx.katherine_tpx3_px_event_count_itot_t *> px
    (<Acquisition> user_ctx).observer.pixels_received([Tpx3PxEventCountItot(cdata=dpx[i]) for i in range(count)])

def MD_SIZE():
   return cacquisition.KATHERINE_MD_SIZE


# --- 1.x names ---------------------------------------------------------
#
# The C API keeps its 1.x spellings in katherine/katherine1.h; this is the
# same courtesy for Python, and removal is planned for the same release.

#: What each 1.x class is called now, or None where 2.0 has no equivalent.
_RENAMED_IN_2_0 = {
    'Dacs':             'Tpx3Dacs',
    'PxConfig':         'Tpx3PxConfig',
    'TestPulseConfig':  'Tpx3TestPulseConfig',
    'PxToaTot':         'Tpx3PxToaTot',
    'PxToaOnly':        'Tpx3PxToaOnly',
    'PxFastToaTot':     'Tpx3PxFastToaTot',
    'PxFastToaOnly':    'Tpx3PxFastToaOnly',
    'PxEventItot':      'Tpx3PxEventCountItot',
    'PxFastEventItot':  'Tpx3PxFastEventCountItot',
    'Freq':             'Tpx3Freq',
    'Phase':            'Tpx3Phase',
    'AcquisitionMode':  'Tpx3PxMode',
    'ReadoutType':      'Tpx3ReadoutMode',
    'Udp':              None,
}


def __getattr__(name):
    """Resolve a 1.x class name to its 2.0 spelling, with a warning.

    Deprecated since 2.0 and planned for removal in 3.0, which is what
    katherine/katherine1.h promises for the C names these follow.
    """
    if name not in _RENAMED_IN_2_0:
        raise AttributeError("module 'katherine' has no attribute %r" % name)

    renamed = _RENAMED_IN_2_0[name]
    if renamed is None:
        raise AttributeError(
            "katherine.%s was removed in 2.0: the UDP session type is private "
            "now, and a device is opened with katherine.Device('udp://...')" % name)

    # stacklevel 1, not the usual 2: this __getattr__ is compiled and has no
    # Python frame of its own, so 2 attributes the warning above the caller
    # and the default filters then drop it.
    _warnings.warn(
        "katherine.%s is deprecated since 2.0 and becomes katherine.%s; "
        "see docs/migration-1.x.md" % (name, renamed),
        DeprecationWarning, stacklevel=1)
    return globals()[renamed]
