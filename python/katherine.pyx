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
from libc.string cimport memcpy, memset
from libcpp cimport bool
# Imported under private names on purpose. A module-scope `from enum import
# Enum` makes katherine.Enum a public attribute of this module and pulls it
# into `from katherine import *`, which is no part of this binding's surface;
# the underscore keeps both out, `import *` skipping underscored names by
# itself. Hence no __all__: the public surface is everything not underscored,
# which is the rule Python already applies.
from enum import Enum as _Enum, unique as _unique
import array as _array

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


def check_return_code(int res):
    if res == 0:
        return

    message = cerror.katherine_strerror(res).decode('UTF-8')

    if res == cerror.KATHERINE_E_TIMEOUT:
        raise TimeoutError(message)
    elif res == cerror.KATHERINE_E_INVAL:
        raise ValueError(message)
    elif res == cerror.KATHERINE_E_NOMEM:
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
        return ctdc.katherine_tdc_source_str(self.value).decode('UTF-8')


@_unique
class PulseSource(_Enum):
    """Where the pulse generator takes its trigger."""

    RISING_EDGE  = cpulse.katherine_pulse_source_t.KATHERINE_PULSE_SOURCE_RISING_EDGE
    FALLING_EDGE = cpulse.katherine_pulse_source_t.KATHERINE_PULSE_SOURCE_FALLING_EDGE
    OSCILLATOR   = cpulse.katherine_pulse_source_t.KATHERINE_PULSE_SOURCE_OSCILLATOR

    def __str__(self):
        """Render as the C library does, e.g. 'oscillator'."""
        return cpulse.katherine_pulse_source_str(self.value).decode('UTF-8')


class PulseOutput(_Enum):
    """What a generator channel puts out."""
    PULSE          = cpulse.katherine_pulse_output_t.KATHERINE_PULSE_OUTPUT_PULSE
    PULSE_INVERTED = cpulse.katherine_pulse_output_t.KATHERINE_PULSE_OUTPUT_PULSE_INVERTED
    PASSTHROUGH          = cpulse.katherine_pulse_output_t.KATHERINE_PULSE_OUTPUT_PASSTHROUGH
    PASSTHROUGH_INVERTED = cpulse.katherine_pulse_output_t.KATHERINE_PULSE_OUTPUT_PASSTHROUGH_INVERTED

    def __str__(self):
        """Render as the C library does, e.g. 'pulse_inverted'."""
        return cpulse.katherine_pulse_output_str(self.value).decode('UTF-8')


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
    def supported(self):
         """Whether this library can drive this readout. Also how a caller
         learns the readout never answered: constructing a Device succeeds
         either way, the enumeration inside it being best-effort."""
         return self._c_info.supported


cdef class Device:
    cdef cdevice.katherine_device_t* _c_device
    def __cinit__(self, address):
         self._c_device = <cdevice.katherine_device_t*> PyMem_Malloc(sizeof(cdevice.katherine_device_t))
         if self._c_device is NULL:
             raise MemoryError()

         res = cdevice.katherine_device_init(self._c_device, address.encode(), 0)
         check_return_code(res)

    def __dealloc__(self):
         if self._c_device is not NULL:
             cdevice.katherine_device_fini(self._c_device)
             PyMem_Free(self._c_device)
             self._c_device = NULL


    def set_strict_ack(self, bint strict):
         ctransport.katherine_transport_set_strict_ack(&self._c_device.control_transport, strict)

    @property
    def stray_command_responses(self):
         return self._c_device.control_transport.stray_command_responses

    def __repr__(self):
         return _snprint_repr(<snprint_fn_t> cdevice.katherine_device_snprint, self._c_device)

    def can_correct_timestamp_phase(self):
         return cdevice.katherine_device_can_correct_timestamp_phase(self._c_device)

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

    def enumerate(self):
         """Ask the readout what it is, filling in what the generation-dependent
         calls need.

         katherine.Device() does this on construction, but a readout that was
         not yet listening then leaves it undone -- and an un-enumerated device
         refuses those calls rather than guessing a generation. Call this once
         the readout is demonstrably answering.
         """
         res = cdevice.katherine_device_enumerate(self._c_device)
         check_return_code(res)

    def get_chip_id(self):
         cdef char[:] chip_id = _array.array('b', [0] * cstatus.KATHERINE_CHIP_ID_STR_SIZE)
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
         res = cstatus.katherine_tpx3_perform_digital_test(self._c_device)
         check_return_code(res)

    def get_adc_voltage(self, unsigned char channel_id):
         cdef float voltage
         res = cstatus.katherine_get_adc_voltage(self._c_device, channel_id, &voltage)
         check_return_code(res)
         return voltage

    def set_test_pulses(self, Tpx3TestPulseConfig test_pulse_config):
         res = cconfig.katherine_tpx3_set_test_pulses(self._c_device, &test_pulse_config._c_test_pulse_config)
         check_return_code(res)

    def configure(self, Config config):
         res = cconfig.katherine_configure(self._c_device, &config._c_config)
         check_return_code(res)

    def set_all_pixel_config(self, Tpx3PxConfig px_config):
         res = cconfig.katherine_tpx3_set_all_pixel_config(self._c_device, &px_config._c_px_config)
         check_return_code(res)

    def set_acq_time(self, double ns):
         res = cconfig.katherine_set_acq_time(self._c_device, ns)
         check_return_code(res)

    def set_acq_mode(self, px_mode, bool fast_vco_enabled):
         res = cconfig.katherine_tpx3_set_acq_mode(self._c_device, px_mode.value, fast_vco_enabled)
         check_return_code(res)

    def set_no_frames(self, int no_frames):
         res = cconfig.katherine_set_no_frames(self._c_device, no_frames)
         check_return_code(res)

    def set_bias(self, unsigned char bias_id, float bias_value):
         res = cconfig.katherine_set_bias(self._c_device, bias_id, bias_value)
         check_return_code(res)

    def set_seq_readout_start(self, int arg):
         res = cconfig.katherine_tpx3_set_seq_readout_start(self._c_device, arg)
         check_return_code(res)

    def set_acquisition_triggers(self, Trigger start_trigger, Trigger end_trigger):
         res = cconfig.katherine_set_acquisition_triggers(self._c_device, &start_trigger._c_trigger, &end_trigger._c_trigger)
         check_return_code(res)

    def set_sensor_register(self, reg_idx, int32_t reg_value):
         res = cconfig.katherine_tpx3_set_sensor_register(self._c_device, reg_idx.value, reg_value)
         check_return_code(res)

    def update_sensor_registers(self):
         res = cconfig.katherine_tpx3_update_sensor_registers(self._c_device)
         check_return_code(res)

    def output_block_config_update(self):
         res = cconfig.katherine_tpx3_output_block_config_update(self._c_device)
         check_return_code(res)

    def timer_set(self):
         res = cconfig.katherine_tpx3_timer_set(self._c_device)
         check_return_code(res)

    @property
    def derived_info(self):
         """What this readout is, as recognized from its hardware type."""
         cdef DeviceDerivedInfo info = DeviceDerivedInfo()
         info._c_info = self._c_device.derived_info
         return info

    def set_dacs(self, Tpx3Dacs dacs, uint8_t chip_index=0):
         """Write all eighteen DACs and latch them into the chip.

         chip_index trails the values here, rather than leading them as it
         does in C, so that callers written against 1.x keep working."""
         res = cconfig.katherine_tpx3_set_dacs(self._c_device, chip_index, &dacs._c_dacs)
         check_return_code(res)

    def set_dac(self, dac, uint16_t value, uint8_t chip_index=0):
         """Write one DAC and latch it into the chip.

         The other seventeen keep whatever the readout last held, which is
         its own memory and is lost on a power cycle; set_dacs() is how a
         caller establishes a baseline it knows."""
         res = cconfig.katherine_tpx3_set_dac(self._c_device, chip_index, dac.value, value)
         check_return_code(res)

    def get_bias(self, unsigned char bias_id):
         cdef float voltage
         res = cstatus.katherine_get_bias(self._c_device, bias_id, &voltage)
         check_return_code(res)
         return voltage

    def get_bias_leakage(self, unsigned char bias_id):
         """Volts across the sense resistor, not amperes: the conversion needs
         a per-unit zero, which is the caller's to calibrate."""
         cdef float voltage
         res = cstatus.katherine_get_bias_leakage(self._c_device, bias_id, &voltage)
         check_return_code(res)
         return voltage

    def get_dac_monitor_voltage(self, uint8_t chip_index, monitor):
         cdef float voltage
         res = cstatus.katherine_tpx3_get_dac_monitor_voltage(self._c_device, chip_index, monitor.value, &voltage)
         check_return_code(res)
         return voltage

    def get_dac_monitor_voltages(self, uint8_t chip_index):
         cdef Tpx3DacVoltages voltages = Tpx3DacVoltages()
         res = cstatus.katherine_tpx3_get_dac_monitor_voltages(self._c_device, chip_index, &voltages._c_voltages)
         check_return_code(res)
         return voltages

    def set_gpio_config(self, channels):
         """Takes a sequence of GpioChannelConfig, one per channel."""
         cdef cgpio.katherine_gpio_config_t config
         _fill_gpio_config(&config, channels)
         res = cgpio.katherine_set_gpio_config(self._c_device, &config)
         check_return_code(res)

    def set_composed_ready_signal(self, channels):
         cdef cgpio.katherine_gpio_config_t config
         _fill_gpio_config(&config, channels)
         res = cgpio.katherine_set_composed_ready_signal(self._c_device, &config)
         check_return_code(res)

    def probe_gpio(self):
         cdef GpioMonitor state = GpioMonitor()
         res = cgpio.katherine_probe_gpio(self._c_device, &state._c_state)
         check_return_code(res)
         return state

    def set_tdc_config(self, channels):
         """Takes a sequence of TdcChannelConfig, one per channel."""
         cdef ctdc.katherine_tdc_config_t config
         _fill_tdc_config(&config, channels)
         res = ctdc.katherine_set_tdc_config(self._c_device, &config)
         check_return_code(res)

    def get_tdc_counts(self):
         cdef uint32_t counts[ctdc.KATHERINE_MAX_TDC_CHANNEL_COUNT]
         res = ctdc.katherine_get_tdc_counts(self._c_device, counts)
         check_return_code(res)
         return [counts[i] for i in range(ctdc.KATHERINE_MAX_TDC_CHANNEL_COUNT)]

    def set_pulse_generator(self, PulseConfig config):
         res = cpulse.katherine_set_pulse_generator(self._c_device, &config._c_config)
         check_return_code(res)

    def get_pulse_generator_counts(self):
         cdef PulseCounts counts = PulseCounts()
         res = cpulse.katherine_get_pulse_generator_counts(self._c_device, &counts._c_counts)
         check_return_code(res)
         return counts

    def reset_pulse_generator_counts(self):
         res = cpulse.katherine_reset_pulse_generator_counts(self._c_device)
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


cdef class Trigger:
    cdef cconfig.katherine_trigger_t _c_trigger

    def __init__(self, enabled=False, channel=0, edge=None, delayed=False, delay_seconds=0.0, cdata=None):
         if cdata is None:
             self._c_trigger.enabled = enabled
             self._c_trigger.channel = channel
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
         return self._c_trigger.channel

    @channel.setter
    def channel(self, val):
         self._c_trigger.channel = val

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


@_unique
class Tpx3Reg(_Enum):
    TEST_PULSE_METHOD     = cconfig.katherine_tpx3_reg_t.KATHERINE_TPX3_REG_TEST_PULSE_METHOD
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
    def polarity(self):
       return Polarity(self._c_config.polarity)

    @polarity.setter
    def polarity(self, val):
       self._c_config.polarity = Polarity(val).value

    # A request only: what actually happens depends on the device and on the
    # phase count, and is reported afterwards by Acquisition.phase_correction,
    # not by this property.
    @property
    def correct_phase(self):
       return self._c_config.correct_phase

    @correct_phase.setter
    def correct_phase(self, val):
       self._c_config.correct_phase = val

    @property
    def phase(self):
       return Tpx3Phase(self._c_config.phase)

    @phase.setter
    def phase(self, val):
       self._c_config.phase = val.value

    @property
    def freq(self):
       return Tpx3Freq(self._c_config.freq)

    @freq.setter
    def freq(self, val):
       self._c_config.freq = val.value

    @property
    def dacs(self):
       return Tpx3Dacs(cdata=self._c_config.dacs)

    cdef _set_dacs(self, Tpx3Dacs val):
         memcpy(&self._c_config.dacs, &val._c_dacs, sizeof(self._c_config.dacs))

    @dacs.setter
    def dacs(self, val):
         self._set_dacs(val)

    @property
    def pixel_config(self):
       return Tpx3PxConfig(cdata=self._c_config.pixel_config)

    cdef _set_pixel_config(self, Tpx3PxConfig val):
         memcpy(&self._c_config.pixel_config, &val._c_px_config, sizeof(self._c_config.pixel_config))

    @pixel_config.setter
    def pixel_config(self, val):
         self._set_pixel_config(val)

    @property
    def test_pulse_config(self):
       return Tpx3TestPulseConfig(cdata=self._c_config.test_pulse_config)

    cdef _set_test_pulse_config(self, Tpx3TestPulseConfig val):
         memcpy(&self._c_config.test_pulse_config, &val._c_test_pulse_config, sizeof(self._c_config.test_pulse_config))

    @test_pulse_config.setter
    def test_pulse_config(self, val):
         self._set_test_pulse_config(val)


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

    def begin(self, Config config, readout_type, px_mode, bool fast_vco_enabled, bool decode_data=True):
      if fast_vco_enabled:
        if px_mode == Tpx3PxMode.TOA_TOT:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_f_toa_tot
        elif px_mode == Tpx3PxMode.ONLY_TOA:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_f_toa_only
        elif px_mode == Tpx3PxMode.EVENT_COUNT_ITOT:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_f_event_count_itot
      else:
        if px_mode == Tpx3PxMode.TOA_TOT:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_toa_tot
        elif px_mode == Tpx3PxMode.ONLY_TOA:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_toa_only
        elif px_mode == Tpx3PxMode.EVENT_COUNT_ITOT:
          self._c_acq.handlers.pixels_received = _forward_pixels_received_event_count_itot

      res = cacquisition.katherine_acquisition_begin(self._c_acq, &config._c_config, readout_type.value, px_mode.value, fast_vco_enabled, decode_data)
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
       return Tpx3ReadoutMode(self._c_acq.readout_mode)

    @property
    def px_mode(self):
       return Tpx3PxMode(self._c_acq.px_mode)

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
    def phase_correction(self):
       return Tpx3PhaseCorrection(self._c_acq.phase_correction)

    @property
    def phase_count(self):
       return self._c_acq.phase_count

    @property
    def frame_active(self):
       return self._c_acq.frame_active

    def timestamp_phase_offset(self, int x, int y):
       return cacquisition.katherine_acquisition_timestamp_phase_offset(self._c_acq, Tpx3PxConfig._coord(x, y))


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
