#!/usr/bin/env python3
# Smoke test for the Python bindings against the ksim daemon.
#
# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

"""Drives the compiled 'katherine' extension module over real UDP loopback
sockets against ksim, the standalone daemon hosting the protocol emulator
(tools/ksim). It is the Python counterpart of c/tests/test_e2e_acq.c: same
daemon, same command line, same ground truth, but through the Python
bindings rather than the C API directly.

Usage: smoke.py <path-to-ksim> [module-dir]

<path-to-ksim> is the ksim executable to spawn. [module-dir] is prepended to
sys.path so that the freshly built 'katherine' module is importable before
any installed copy; it can be given instead (or on top of an already
importable module) via the KATHERINE_PYTHON_MODULE_DIR environment variable,
which is how this script is normally invoked -- see the add_test() call in
python/CMakeLists.txt.

Only failures that happen before the daemon has answered a single command
with its own identifier are reported as an environmental skip (TAP
"1..0 # SKIP <reason>", exit code 77, mirroring c/tests/test_e2e_acq.c's
skip contract for CTest). Every later failure is a genuine assertion failure.
"""

import os
import re
import subprocess
import sys
import time

KSIM_LISTEN_ADDRESS = '127.0.0.2'
# The same readout as a device address, which is what the public API takes.
KSIM_DEVICE_ADDRESS = 'udp://' + KSIM_LISTEN_ADDRESS
KSIM_SEED = 12345
HITS_PER_FRAME = 40
LOST_PER_FRAME = 3

# The katherine C extension raises one of these for a device call that
# failed with a library error code (see katherine.pyx's check_return_code());
# each is constructed from katherine_strerror(), so str(e) is already the
# message a caller wants to show.
KATHERINE_ERRORS = (TimeoutError, ValueError, MemoryError, RuntimeError)

# Readiness polling: the device's control timeout is 100 ms, so one failed
# attempt costs roughly that much plus the sleep below -- 80 attempts bound
# the wait at roughly ten seconds.
READY_ATTEMPTS = 80
READY_SLEEP_S = 0.025

# Emulated readout profile defaults, from katherine_emu_profile_defaults() in
# c/src/emu/emulator.c (see c/tests/test_e2e_acq.c for the same constants).
EXPECTED_CHIP_ID = 'A1-W0001'
EXPECTED_HW_TYPE = 0x01
EXPECTED_HW_REVISION = 0x01
EXPECTED_SERIAL = 1
EXPECTED_FW_VERSION = 1
EXPECTED_READOUT_T = 30.0
EXPECTED_SENSOR_T = 40.0

EXPECTED_COMM_LINES = 0xFF
EXPECTED_DATA_RATE = 0

ADC_CHANNEL = 3
EXPECTED_ADC_VOLTAGE = 0.5

# Period of the emulated readout timer, i.e. the unit of every timestamp in
# the measurement data stream (KATHERINE_EMU_TICK_NS in c/src/emu/emu.h).
TICK_NS = 25

# Shutter short enough that the coarse ToA cannot wrap within a frame (see
# c/tests/test_e2e_acq.c for the same value and reasoning): 0.4 ms is 16000
# readout ticks, the exact difference the frame's start/end timestamps must
# show.
SHORT_ACQ_TIME_NS = 400000.0
EXPECTED_FRAME_TICKS = int(SHORT_ACQ_TIME_NS / TICK_NS)

KSIM_STOP_TIMEOUT_S = 5.0


class Tap:
    """Minimal TAP producer, the Python counterpart of c/tests/ktest.h."""

    def __init__(self):
        self.count = 0
        self.failed = 0

    def check(self, description, cond, detail=None):
        self.count += 1
        if cond:
            print('ok %d - %s' % (self.count, description))
        else:
            self.failed += 1
            line = 'not ok %d - %s' % (self.count, description)
            if detail is not None:
                line += ' (%s)' % detail
            print(line)
        sys.stdout.flush()

    def check_eq(self, description, actual, expected):
        self.check(description, actual == expected, '%r != %r' % (actual, expected))

    def check_raises(self, description, exc_type, fn, *args):
        """Requires fn(*args) to raise exactly exc_type.

        By type, not by category. smoke.py used to catch
        (TimeoutError, ValueError, MemoryError, RuntimeError) as one tuple,
        which is how the binding's error mapping could compare against negated
        codes for a whole release -- every failure arrived as RuntimeError and
        every test still passed (see 9bd40ed, and #33).
        """
        try:
            fn(*args)
        except exc_type:
            self.check(description, True)
        except BaseException as e:
            self.check(description, False, 'raised %s: %s' % (type(e).__name__, e))
        else:
            self.check(description, False, 'did not raise')

    def comment(self, text):
        print('# %s' % text)
        sys.stdout.flush()


def skip(reason):
    print('1..0 # SKIP %s' % reason)
    sys.stdout.flush()
    return 77


def stop_ksim(proc):
    """Terminates and reaps ksim, on every path out of main()."""
    if proc.poll() is not None:
        return

    proc.terminate()
    try:
        proc.wait(timeout=KSIM_STOP_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def wait_ready(ksim, katherine):
    """Waits for ksim to answer a chip-identifier request with its own
    identifier, recreating the device between attempts. Returns
    (device, None) once ready, or (None, reason) if the environment cannot
    host the daemon -- the only condition this script treats as a skip.

    Readiness is the *content* of the answer, not merely a successful
    receive, and the device is torn down and recreated between attempts: a
    command sent before the daemon has bound its address is not lost (every
    127.0.0.0/8 address is local), but delivered straight back to the
    library's own wildcard-bound control socket, which then reads its own
    command as the readout's acknowledgement. The device's sessions pin their
    remote address, so a later genuine answer cannot be confused with that
    echo, but a fresh device is still cheap insurance against anything else a
    failed attempt may have left queued.
    """
    for _ in range(READY_ATTEMPTS):
        try:
            device = katherine.Device(KSIM_DEVICE_ADDRESS)
        except KATHERINE_ERRORS as e:
            return None, 'cannot bind the local control/data ports 1555/1556: %s' % e

        try:
            chip_id = device.get_chip_id()
        except KATHERINE_ERRORS:
            chip_id = None

        if chip_id == EXPECTED_CHIP_ID:
            # Readiness is probed with the chip id, not with enumeration, so
            # the enumerate inside Device() may have run before ksim was
            # listening. Ask again now that it is answering, or the
            # generation-dependent calls refuse a device whose generation was
            # never learned.
            try:
                device.enumerate()
            except KATHERINE_ERRORS as e:
                return None, 'readout answered its chip id but not its status: %s' % e

            return device, None

        del device

        if ksim.poll() is not None:
            return None, ('ksim exited during startup: it could not bind %s (not aliased by default on macOS), '
                           'or could not be executed at all' % KSIM_LISTEN_ADDRESS)

        time.sleep(READY_SLEEP_S)

    return None, ('no answer from ksim at %s:1555 within %d attempts: the local ports may be bound on the wildcard '
                  'address by another process, or the wildcard/specific same-port coexistence this needs may be '
                  'unsupported here (unproven on Windows)' % (KSIM_LISTEN_ADDRESS, READY_ATTEMPTS))


def find_project_version(script_path):
    """Best-effort lookup of the CMake project() version, read from the
    repository's top-level CMakeLists.txt two directories up from this
    script (python/tests/smoke.py), with the KATHERINE_VERSION_SUFFIX cache
    variable (e.g. "-dev" during a development cycle) appended when the
    file sets one, exactly as version.h.in composes KATHERINE_VERSION_STRING
    and __version__ follows it. Returns None if that file cannot be found or
    parsed, e.g. when this script has been copied out of a source checkout
    -- in which case __version__ is checked for shape only."""
    repo_root = os.path.abspath(os.path.join(os.path.dirname(script_path), os.pardir, os.pardir))
    path = os.path.join(repo_root, 'CMakeLists.txt')

    try:
        with open(path, 'r') as f:
            text = f.read()
    except OSError:
        return None

    m = re.search(r'project\(katherine\s+VERSION\s+([0-9.]+)', text)
    if m is None:
        return None

    version = m.group(1)

    suffix = re.search(r'set\(KATHERINE_VERSION_SUFFIX\s+"([^"]*)"\)', text)
    if suffix is not None:
        version += suffix.group(1)

    return version


def build_config(katherine):
    """A configuration equivalent to the one c/tests/test_e2e_acq.c's
    configure() and python/examples/krun.py's configure() use, less the
    pixel matrix: the emulated readout models the upload protocol (it counts
    the 16384 configuration words and acknowledges them) but not the matrix
    contents, so the all-zero matrix a fresh Tpx3PxConfig starts out with is
    uploaded and accepted like any other."""
    c = katherine.Config()

    c.bias_id = 0
    c.acq_time = SHORT_ACQ_TIME_NS
    c.no_frames = 1
    c.bias = 230

    c.start_trigger = katherine.Trigger(enabled=False)
    c.stop_trigger = katherine.Trigger(enabled=False)

    c.gray_disable = False
    c.polarity = katherine.Polarity.ELECTRONS

    c.phase = katherine.Tpx3Phase.PHASE_1
    c.freq = katherine.Tpx3Freq.FREQ_40

    dacs = katherine.Tpx3Dacs()
    dacs.Ibias_Preamp_ON = 128
    dacs.Ibias_Preamp_OFF = 8
    dacs.Vpreamp_NCAS = 128
    dacs.Ibias_Ikrum = 15
    dacs.Vfbk = 164
    dacs.Vthreshold_fine = 476
    dacs.Vthreshold_coarse = 8
    dacs.Ibias_DiscS1_ON = 100
    dacs.Ibias_DiscS1_OFF = 8
    dacs.Ibias_DiscS2_ON = 128
    dacs.Ibias_DiscS2_OFF = 8
    dacs.Ibias_PixelDAC = 128
    dacs.Ibias_TPbufferIn = 128
    dacs.Ibias_TPbufferOut = 128
    dacs.VTP_coarse = 128
    dacs.VTP_fine = 256
    dacs.Ibias_CP_PLL = 128
    dacs.PLL_Vcntrl = 128
    c.dacs = dacs

    c.pixel_config = katherine.Tpx3PxConfig()

    return c


def make_probe(katherine):
    """An AcquisitionObserver counting delivered hits and capturing the
    frame_ended() call of the single frame this script acquires. Built
    inside a function (rather than at module scope) so that its base class
    can come from whichever 'katherine' module main() ends up importing --
    the freshly built one, via sys.path, on a normal CTest run."""

    class Probe(katherine.AcquisitionObserver):
        def __init__(self):
            self.hits_delivered = 0
            self.frame_info = None

        def pixels_received(self, pixels):
            self.hits_delivered += len(pixels)

        def frame_ended(self, frame_idx, completed, info):
            self.frame_info = info

    return Probe()


def run_acquisition(katherine, device):
    config = build_config(katherine)

    # Sizes mirror c/tests/test_e2e_acq.c: the measurement data buffer only
    # has to hold one datagram with room to spare, the pixel buffer a whole
    # frame, so that the hits of the one frame under test reach the handler
    # in one call.
    acq = katherine.Acquisition(
        device, katherine.MD_SIZE() * 4096, katherine.Tpx3PxFastToaTot.RAW_SIZE() * 1024, 500, 10000)
    probe = make_probe(katherine)
    acq.observer = probe

    acq.begin(config, katherine.Tpx3ReadoutMode.DATA_DRIVEN, katherine.Tpx3PxMode.TOA_TOT, True)
    acq.read()

    return acq, probe


def check_reprs(tap, katherine, device):
    """Debug-stringification checks (repr.c via the __repr__ methods added
    to katherine.pyx): golden-string byte-identity for objects that need no
    hardware, copied verbatim from the same-valued cases in
    c/tests/test_repr.c so a change to the C rendering that the Python
    binding fails to pick up shows up here; plus non-empty/right-prefix
    sanity checks for a couple of objects that do need the daemon (their
    field values are already checked elsewhere in run_checks())."""

    tp = katherine.Tpx3TestPulseConfig(enabled=True, digital_only=False, external=False, count=100, period=65, phase=0)
    tap.check_eq('Tpx3TestPulseConfig repr matches the C golden byte-for-byte', repr(tp),
        'test_pulse_config{enabled: true, digital_only: false, external: false, count: 100, period: 65, phase: 0}')

    trig = katherine.Trigger(enabled=True, channel=3, edge=katherine.TriggerEdge.RISING)
    tap.check_eq('Trigger repr matches the C golden byte-for-byte', repr(trig),
        'trigger{enabled: true, channel: 3, edge: rising, delayed: false, delay_seconds: 0}')

    px = katherine.Tpx3PxConfig()
    tap.check_eq('Tpx3PxConfig repr matches the C golden byte-for-byte (all-zero matrix)', repr(px),
        'px_config{words: 16384, xor64: 0x0000000000000000}')

    rs_repr = repr(device.get_readout_status())
    tap.check('Device.get_readout_status() repr is non-empty and prefixed',
        rs_repr.startswith('readout_status{') and rs_repr.endswith('}') and len(rs_repr) > len('readout_status{}'))

    comm_repr = repr(device.get_comm_status())
    tap.check('Device.get_comm_status() repr is non-empty and prefixed',
        comm_repr.startswith('comm_status{') and comm_repr.endswith('}') and len(comm_repr) > len('comm_status{}'))

    device_repr = repr(device)
    tap.check('Device repr is non-empty and prefixed',
        device_repr.startswith('device{') and device_repr.endswith('}') and len(device_repr) > len('device{}'))


def check_enums(tap, katherine):
    """Every enumerator of the C headers reaches Python.

    Guards one specific way the bindings rot: the C enum grows and this layer
    does not follow, leaving a setting nameless from Python while everything
    still compiles and every other test passes. Tpx3Freq lost FREQ_20 that way.
    Spelled out as names and values rather than counted, so a member that is
    renamed or given the wrong value fails too.
    """
    tap.check_eq('Tpx3Freq enumerates every frequency',
                 [(m.name, m.value) for m in katherine.Tpx3Freq],
                 [('FREQ_20', 0), ('FREQ_40', 1), ('FREQ_80', 2), ('FREQ_160', 3)])
    tap.check_eq('Tpx3Phase enumerates every phase count',
                 [(m.name, m.value) for m in katherine.Tpx3Phase],
                 [('PHASE_1', 0), ('PHASE_2', 1), ('PHASE_4', 2), ('PHASE_8', 3), ('PHASE_16', 4)])
    tap.check_eq('Tpx3PxMode enumerates every pixel mode',
                 [(m.name, m.value) for m in katherine.Tpx3PxMode],
                 [('TOA_TOT', 0), ('ONLY_TOA', 1), ('EVENT_COUNT_ITOT', 2)])
    tap.check_eq('Tpx3ReadoutMode enumerates every readout mode',
                 [(m.name, m.value) for m in katherine.Tpx3ReadoutMode],
                 [('FRAME_BASED', 0), ('DATA_DRIVEN', 1)])
    tap.check_eq('AcquisitionState enumerates every state',
                 [(m.name, m.value) for m in katherine.AcquisitionState],
                 [('NOT_STARTED', 0), ('RUNNING', 1), ('SUCCEEDED', 2), ('TIMED_OUT', 3)])
    # HOLES is zero, and must stay zero: a zeroed configuration has to select
    # the polarity that cannot destroy the chip. Checked by value here, not
    # only by name.
    tap.check_eq('Polarity enumerates every carrier',
                 [(m.name, m.value) for m in katherine.Polarity],
                 [('HOLES', 0), ('ELECTRONS', 1)])
    tap.check_eq('Tpx3PhaseCorrection enumerates every outcome',
                 [(m.name, m.value) for m in katherine.Tpx3PhaseCorrection],
                 [('NONE', 0), ('SOFTWARE', 1), ('HARDWARE', 2)])
    # TEST_PULSE_PERIOD is absent on purpose: C aliases it to
    # TEST_PULSE_METHOD, both naming register 0, and @unique forbids an alias.
    # Python would not iterate it even without @unique, so this list is the
    # whole of the enumeration either way.
    tap.check_eq('Tpx3Reg enumerates every sensor register',
                 [(m.name, m.value) for m in katherine.Tpx3Reg],
                 [('TEST_PULSE_METHOD', 0), ('NUMBER_TEST_PULSES', 1),
                  ('OUT_BLOCK_CONFIG', 2), ('PLL_CONFIG', 3), ('GENERAL_CONFIG', 4),
                  ('SLVS_CONFIG', 5), ('POWER_PULSING_PATTERN', 6),
                  ('SET_TIMER_LOW', 7), ('SET_TIMER_MID', 8), ('SET_TIMER_HIGH', 9),
                  ('SENSE_DAC_SELECTOR', 10), ('EXT_DAC_SELECTOR', 11)])



def check_enum_rendering(tap, katherine):
    """Each enumeration renders as the C library does, through str().

    __str__ rather than __repr__ deliberately: Enum's default __repr__ carries
    the class, the member name and the value, which is strictly more than the
    token, so overriding it would cost the REPL and tracebacks their identity.
    Overriding __str__ instead makes str(), print(), f-strings and %s all agree
    with the C library and with the C++ operator<<.

    Asserted per member rather than spot-checked, since a __str__ wired to the
    wrong stringifier would still return a plausible lowercase token.
    """
    expected = {
        'Polarity':        ['holes', 'electrons'],
        'Tpx3Phase':       ['phase_1', 'phase_2', 'phase_4', 'phase_8', 'phase_16'],
        'Tpx3Freq':        ['freq_20', 'freq_40', 'freq_80', 'freq_160'],
        'Tpx3PxMode':      ['toa_tot', 'only_toa', 'event_count_itot'],
        # The member names are Python's, the strings are the C library's, and
        # for this one they disagree: the C enumerator is READOUT_SEQUENTIAL.
        'Tpx3ReadoutMode': ['sequential', 'data_driven'],
        'AcquisitionState': ['not_started', 'running', 'succeeded', 'timed_out'],
        'Tpx3PhaseCorrection': ['none', 'software', 'hardware'],
    }
    for name, strings in expected.items():
        cls = getattr(katherine, name)
        tap.check_eq('%s renders as the C library does' % name, [str(m) for m in cls], strings)

    # repr is untouched, and still the one place the identity appears whole.
    tap.check_eq('repr keeps the enumeration identity',
                 repr(katherine.Polarity.HOLES), '<Polarity.HOLES: 0>')

    # Tpx3Reg has no C stringifier, so it keeps Enum's default rendering
    # rather than being given an invented one.
    tap.check_eq('Tpx3Reg keeps the default rendering',
                 str(katherine.Tpx3Reg.TEST_PULSE_METHOD), 'Tpx3Reg.TEST_PULSE_METHOD')

    # The str_* family is not bound as module functions: an enumeration renders
    # itself, and a caller wanting the C text streams or str()s the value.
    for gone in ('str_acquisition_status', 'str_phase_correction'):
        tap.check_eq('%s is not a module function' % gone, hasattr(katherine, gone), False)

def check_readout_peripherals(tap, katherine):
    """The GPIO channels, the timing unit and the pulse generator reach
    Python: their enumerations, their sizes, and the objects a caller builds
    to configure them.

    No device here, so nothing is sent -- the claim is the weak one WP20 asks
    for, that every object declared can be built and every property read.
    """
    tap.check_eq('GpioMapping enumerates every mapping',
                 len(list(katherine.GpioMapping)), 33)
    tap.check_eq('GpioMapping starts at the input',
                 (katherine.GpioMapping.INPUT.name, katherine.GpioMapping.INPUT.value),
                 ('INPUT', 0))
    tap.check_eq('GpioMapping ends at the constant high',
                 katherine.GpioMapping.HIGH.value, 32)
    tap.check_eq('GpioReady enumerates every part',
                 [(m.name, m.value) for m in katherine.GpioReady],
                 [('UNUSED', 0), ('WHEN_HIGH', 1), ('WHEN_LOW', 2)])
    tap.check_eq('GpioMonitorSignal enumerates every state',
                 [(m.name, m.value) for m in katherine.GpioMonitorSignal],
                 [('UNKNOWN', 0), ('CONSTANT', 1), ('PERIODIC', 2)])
    tap.check_eq('TdcSource enumerates every source',
                 len(list(katherine.TdcSource)), 8)
    tap.check_eq('PulseOutput enumerates every output',
                 [(m.name, m.value) for m in katherine.PulseOutput],
                 [('PULSE', 0), ('PULSE_INVERTED', 1), ('PASSTHROUGH', 2), ('PASSTHROUGH_INVERTED', 3)])

    # Rendered by the C library, as every other enumeration is.
    tap.check_eq('GpioMapping renders as the C library does',
                 str(katherine.GpioMapping.SHUTTER), 'shutter')
    tap.check_eq('GpioMonitorSignal renders as the C library does',
                 str(katherine.GpioMonitorSignal.PERIODIC), 'periodic')
    tap.check_eq('TdcSource renders as the C library does',
                 str(katherine.TdcSource.CHIP_2_FEEDBACK), 'chip_2_feedback')
    tap.check_eq('PulseOutput renders as the C library does',
                 str(katherine.PulseOutput.PASSTHROUGH_INVERTED), 'passthrough_inverted')

    # The sizes and units come from the library rather than being repeated.
    tap.check_eq('GPIO_COUNT is the readout\'s channel count', katherine.GPIO_COUNT, 8)
    tap.check_eq('GPIO_TICK_NS is ten nanoseconds', katherine.GPIO_TICK_NS, 10)
    tap.check_eq('TDC_CHANNEL_COUNT is five', katherine.TDC_CHANNEL_COUNT, 5)
    tap.check_eq('PULSE_CHANNEL_COUNT is four', katherine.PULSE_CHANNEL_COUNT, 4)
    tap.check_eq('TPX3_DAC_MONITOR_COUNT is the DACs plus the monitors',
                 katherine.TPX3_DAC_MONITOR_COUNT, 22)

    # The configuration objects, built and read back.
    ch = katherine.GpioChannelConfig(mapping=katherine.GpioMapping.CLOCK, termination=True)
    tap.check_eq('GpioChannelConfig keeps its mapping', ch.mapping, katherine.GpioMapping.CLOCK)
    tap.check('GpioChannelConfig has a repr', len(repr(ch)) > 0)

    tdc_ch = katherine.TdcChannelConfig(enabled=True, source=katherine.TdcSource.GPIO_3)
    tap.check_eq('TdcChannelConfig keeps its source', tdc_ch.source, katherine.TdcSource.GPIO_3)
    tap.check('TdcChannelConfig has a repr', len(repr(tdc_ch)) > 0)

    gen = katherine.PulseConfig(
        channels=[{'enabled': True, 'output': katherine.PulseOutput.PULSE,
                   'length_seconds': 1e-6, 'delay_seconds': 50e-9}],
        source=katherine.PulseSource.OSCILLATOR, oscillator_frequency_hz=1e6)
    tap.check('PulseConfig is constructible', gen is not None)

    # And the states, which a probe would fill. Zeroed here, so every
    # accessor is exercised without a readout.
    state = katherine.GpioMonitor()
    tap.check_eq('a zeroed GpioMonitor reads no level', state.level(0), False)
    tap.check_eq('a zeroed GpioMonitor reports no signal', state.signal(0),
                 katherine.GpioMonitorSignal.UNKNOWN)
    tap.check_eq('a zeroed GpioMonitor has no frequency', state.frequency(0), 0.0)
    tap.check_eq('a zeroed GpioMonitor has no duty cycle', state.duty_cycle(0), 0.0)
    tap.check_eq('a zeroed GpioMonitor counts no ticks', state.high_ticks(0), 0)
    tap.check_eq('a zeroed GpioMonitor counts no low ticks', state.low_ticks(0), 0)
    tap.check('GpioMonitor has a repr', 'gpio_monitor' in repr(state))
    tap.check_raises('GpioMonitor rejects a channel out of range', IndexError, state.level, 8)

    gen_state = katherine.PulseCounts()
    tap.check_eq('a zeroed PulseCounts counts no edges', gen_state.rising_edges, 0)
    tap.check_eq('a zeroed PulseCounts reports no clock', gen_state.clock_mhz, 0)
    tap.check('PulseCounts has a repr', 'PulseCounts' in repr(gen_state))

    voltages = katherine.Tpx3DacVoltages()
    tap.check_eq('DAC voltages are as long as the library says', len(voltages),
                 katherine.TPX3_DAC_MONITOR_COUNT)
    tap.check_eq('zeroed DAC voltages read zero', voltages[0], 0.0)
    tap.check_eq('DAC voltages name their DACs', voltages.name(0), 'Ibias_Preamp_ON')
    tap.check_eq('DAC voltages name their monitors', voltages.name(21), 'Ibias_dac_cas')
    tap.check('DAC voltages have a repr', 'dac_voltages' in repr(voltages))
    tap.check_raises('DAC voltages reject a monitor out of range', IndexError,
                     voltages.__getitem__, 22)

    tap.check_eq('a monitor renders as the manual spells it',
                 str(katherine.Tpx3DacMonitor.BANDGAP_OUTPUT), 'BandGap output')


def check_discovery(tap, katherine):
    """Discovery reaches Python, and finding nothing is not an error.

    Aimed at a loopback address with no readout on it, so the call spends its
    timeout and comes back empty -- which is the claim: an empty subnet and a
    quiet one are indistinguishable, and neither is a failure.
    """
    tap.check_eq('DISCOVER_MIN_PORT is the firmware\'s floor',
                 katherine.DISCOVER_MIN_PORT, 32)

    found = katherine.discover_udp('udp://127.0.0.1', 42700, 50)
    tap.check_eq('discovering nothing returns nothing', found, [])

    tap.check_raises('discovery rejects no room for an answer', ValueError,
                     katherine.discover_udp, '127.0.0.1', 42700, 50, 0)
    # ValueError rather than OSError: the library refuses the argument before
    # a socket is involved, and check_return_code maps that to ValueError.
    tap.check_raises('discovery rejects a reply port the firmware would ignore',
                     ValueError, katherine.discover_udp, '127.0.0.1', 31, 50)


def check_usb_discovery(tap, katherine):
    """The bus walk reaches Python, and an empty bus is not an error.

    Skipped where the library was built without the USB transport, which is
    the same condition under which the wrapper is not defined.
    """
    if not hasattr(katherine, 'discover_usb'):
        tap.comment('USB discovery absent: the library was built without that transport')
        return

    found = katherine.discover_usb(4)
    tap.check('bus discovery returns no more than was asked for', len(found) <= 4)

    for address, legacy in found:
        tap.check('each answer names its link', address.startswith('usb://'))
        tap.check('no answer on this link claims legacy firmware', legacy is False)

    tap.check_raises('bus discovery rejects no room for an answer', ValueError,
                     katherine.discover_usb, 0)


def check_module_surface(tap, katherine):
    """The module exports its own names and nothing borrowed.

    A module-scope `from enum import Enum` publishes katherine.Enum and drags
    it into `from katherine import *`, which this binding never meant to
    offer. The imports are underscored instead, so Python's own rule -- star
    imports skip underscored names -- bounds the surface without an __all__ to
    keep in step by hand. Asserted because the leak is invisible: nothing
    fails, the name is simply there.
    """
    for borrowed in ('Enum', 'unique', 'array'):
        tap.check_eq('%s is not published by the module' % borrowed,
                     hasattr(katherine, borrowed), False)

    # And what a star import would bind is exactly the public surface, which
    # is what fails if an __all__ is ever added and then drifts.
    public = sorted(n for n in dir(katherine) if not n.startswith('_'))
    namespace = {}
    exec('from katherine import *', namespace)
    tap.check_eq('a star import binds exactly the public names',
                 sorted(n for n in namespace if not n.startswith('__')), public)


def check_timestamp_functions(tap, katherine):
    """The #31 timestamp surface, which shipped with no binding test at all.

    Five module functions, none of them mentioned anywhere in this file until
    now. The C suite already pins their numbers (test_timestamp.c), so nothing
    here restates one: these assert that each is callable across the whole
    frequency enumeration and that the results relate to each other as they
    must, which catches a swapped or mistranscribed pair without this test
    knowing either value.
    """
    for f in katherine.Tpx3Freq:
        ticks = katherine.tpx3_toa_coarse_tick_to_fine_ticks(f)
        shift = katherine.tpx3_toa_coarse_tick_to_fine_shift(f)

        tap.check('fine ticks of %s is a positive int' % f, isinstance(ticks, int) and ticks > 0)
        tap.check('fine shift of %s is a non-negative int' % f, isinstance(shift, int) and shift >= 0)

        # The relation, not the values: the shift is the tick count's exponent.
        tap.check_eq('fine ticks of %s is 1 << shift' % f, 1 << shift, ticks)

        bias = katherine.tpx3_toa_epoch_bias(shift)
        tap.check('epoch bias of %s is a positive int' % f, isinstance(bias, int) and bias > 0)

    shift = katherine.tpx3_toa_coarse_tick_to_fine_shift(katherine.Tpx3Freq.FREQ_40)

    sec_nsec = katherine.tpx3_timestamp_to_seconds(1 << 40)
    tap.check('timestamp_to_seconds returns a (sec, nsec) pair',
              isinstance(sec_nsec, tuple) and len(sec_nsec) == 2)
    tap.check('timestamp_to_seconds yields a whole second and a nanosecond remainder',
              isinstance(sec_nsec[0], int) and isinstance(sec_nsec[1], float))

    toa_ftoa = katherine.tpx3_timestamp_to_toa_ftoa(shift, 0, 1 << 40)
    tap.check('timestamp_to_toa_ftoa returns a (toa, ftoa) pair',
              isinstance(toa_ftoa, tuple) and len(toa_ftoa) == 2)
    tap.check('timestamp_to_toa_ftoa yields two ints',
              all(isinstance(v, int) for v in toa_ftoa))


def check_clock_helpers(tap, katherine):
    """The three pure helpers of config.h, which nothing could reach until now.

    What they answer is pinned by the C suite (test_clock_phases.c,
    test_dacs_validate.c), so nothing here restates a number the manual
    supplies. These assert that the binding reaches the right function: the
    phase count is clamped where Table 17 clamps it, the fine oscillator is
    supported at exactly one frequency, and a DAC past its own width is
    refused while the same value inside it is not.
    """
    for f in katherine.Tpx3Freq:
        for p in katherine.Tpx3Phase:
            granted = katherine.tpx3_actual_phases(f, p)
            tap.check('actual phases of %s at %s is a positive int' % (p, f),
                      isinstance(granted, int) and granted > 0)
            # The divider only ever takes phases away.
            tap.check('actual phases of %s at %s is at most what was asked' % (p, f),
                      granted <= int(str(p).rsplit('_', 1)[1]))

    # Clamped at the fast clocks and granted whole at the slow ones: the one
    # relation that tells a correct binding from one reading the wrong table.
    tap.check('the slowest clock grants all sixteen phases',
              katherine.tpx3_actual_phases(katherine.Tpx3Freq.FREQ_40,
                                           katherine.Tpx3Phase.PHASE_16) == 16)
    tap.check('the fastest clock grants fewer',
              katherine.tpx3_actual_phases(katherine.Tpx3Freq.FREQ_160,
                                           katherine.Tpx3Phase.PHASE_16) < 16)

    supported = [f for f in katherine.Tpx3Freq
                 if katherine.tpx3_freq_is_fast_vco_supported(f)]
    tap.check_eq('the fine oscillator runs at exactly one frequency',
                 supported, [katherine.Tpx3Freq.FREQ_40])

    dacs = katherine.Tpx3Dacs()
    dacs.validate()
    tap.check('an all-zero DAC block validates', True)

    dacs = katherine.Tpx3Dacs()
    dacs.Vthreshold_fine = 0xFFFF
    threw = False
    try:
        dacs.validate()
    except Exception:
        threw = True
    tap.check('a DAC past its own width is refused', threw)


def check_device_declaration(tap, katherine):
    """Opening a readout without asking it anything, and then saying what it is.

    None of this was reachable from Python: the constructor passed flags=0,
    so an address nothing answers could not be opened at all, and without
    that there was nothing to declare. The C++ device test has leaned on the
    same three flags since it was written; this is the Python equivalent.

    Nothing here touches a readout. 127.0.0.3 is the address the C++ test
    uses for the same reason -- bindable, and nobody is listening.
    """
    flags = (katherine.DeviceFlags.DONT_ENUMERATE
             | katherine.DeviceFlags.DONT_CHANGE_UDP_PORTS
             | katherine.DeviceFlags.DONT_SELECT_INTERFACE)

    dev = katherine.Device('udp://127.0.0.3', flags)
    tap.check('an unanswered address opens with all three flags', True)

    # Zeroed until the readout is asked or a caller speaks for it, which is
    # also how the generation-dependent calls know to refuse.
    tap.check('a device nobody declared is unsupported', not dev.derived_info.supported)
    tap.check_eq('a device nobody declared reports hardware type 0', dev.info.hw_type, 0)
    tap.check('DeviceInfo renders', repr(dev.info).startswith('device_info{'))

    # The second-generation row of the hardware-type table, reached without
    # a readout: what declare() exists for.
    dev.declare(katherine.DeviceInfo(hw_type=0x03, chip_count=4))

    tap.check('a declared device is supported', dev.derived_info.supported)
    tap.check_eq('the declared chip count is kept', dev.info.chip_count, 4)
    tap.check_eq('the hardware type names its generation', dev.derived_info.gen, 2)
    tap.check_eq('the hardware type names its chip', dev.derived_info.chip_type,
                 katherine.ChipType.TPX3)

    # The three fields DeviceDerivedInfo did not expose.
    tap.check('max_chip_count is what the hardware could carry',
              dev.derived_info.max_chip_count >= dev.info.chip_count)
    tap.check('tdc_channel_count is reachable', isinstance(dev.derived_info.tdc_channel_count, int))
    tap.check('pulse_channel_count is reachable', isinstance(dev.derived_info.pulse_channel_count, int))

    # The one control call that succeeds without a peer: the readout never
    # acknowledges it, so nothing waits for a reply.
    dev.select_interface()
    tap.check('select_interface() waits for nothing and so needs no readout', True)

    # And the one that does wait.
    raised = False
    try:
        dev.enumerate()
    except Exception:
        raised = True
    tap.check('enumerate() fails where nothing answers', raised)


def check_pixel_types(tap, katherine):
    """Every pixel type, not only the one an acquisition happens to produce.

    run_acquisition() below configures one mode, so it builds exactly one of
    these six and the other five were never touched. Constructing each and
    reading every field is the whole bar here: a property renamed in the .pyx
    or wired to the wrong struct member raises AttributeError or returns the
    wrong kind of value, and neither compiles into a failure anywhere else.
    """
    fields = {
        'Tpx3PxToaTot':             ['chip', 'x', 'y', 'timestamp', 'hit_count', 'tot'],
        'Tpx3PxFastToaTot':         ['chip', 'x', 'y', 'timestamp', 'tot'],
        'Tpx3PxToaOnly':            ['chip', 'x', 'y', 'timestamp', 'hit_count'],
        'Tpx3PxFastToaOnly':        ['chip', 'x', 'y', 'timestamp'],
        'Tpx3PxEventCountItot':     ['chip', 'x', 'y', 'hit_count', 'event_count', 'integral_tot'],
        'Tpx3PxFastEventCountItot': ['chip', 'x', 'y', 'event_count', 'integral_tot'],
    }
    for name, names in fields.items():
        cls = getattr(katherine, name)

        tap.check('%s.RAW_SIZE() is a positive int' % name,
                  isinstance(cls.RAW_SIZE(), int) and cls.RAW_SIZE() > 0)

        px = cls()
        tap.check_eq('%s exposes exactly its documented fields' % name,
                     sorted(m for m in dir(px) if not m.startswith('_') and m != 'RAW_SIZE'),
                     sorted(names))

        values = [getattr(px, f) for f in names]
        tap.check('%s reads every field as an int' % name,
                  all(isinstance(v, int) for v in values))

        # repr goes through the C snprint, so it exercises a second path over
        # the same struct.
        tap.check('%s has a non-empty repr' % name, len(repr(px)) > 0)


def check_error_mapping(tap, katherine):
    """The exception mapping, on a device that is not there.

    The cheapest way to provoke a real library error without hardware, which
    is what makes it the vehicle for the exception-type assertions #33 asks
    for.
    """
    # KATHERINE_E_TIMEOUT on an address nothing answers, and it must arrive as
    # TimeoutError rather than as the RuntimeError catch-all. TEST-NET-1
    # (RFC 5737) rather than loopback, where this file runs a readout
    # emulator of its own.
    tap.check_raises('opening a device nothing answers raises TimeoutError',
                     TimeoutError, katherine.Device, 'udp://192.0.2.1')

    # KATHERINE_E_ADDRESS has no dedicated Python class and falls to the
    # catch-all, which is itself worth pinning: it is the group every
    # unmapped code lands in, so a code that silently stops matching shows up
    # as one of these instead of as the specific class it should have been.
    tap.check_raises('a malformed address raises RuntimeError',
                     RuntimeError, katherine.Device, 'not-an-address')

    # ValueError, but raised by the binding's own length guard rather than
    # routed through check_return_code(). Worth having, and worth being clear
    # about: of the classes asserted here only TimeoutError and RuntimeError
    # exercise the mapping itself, so this one would have passed even while
    # the mapping was broken.
    tap.check_raises('a wrongly sized BMC buffer raises ValueError',
                     ValueError, katherine.Tpx3PxConfig.from_bmc_data, b'too short')

    # MemoryError is deliberately not provoked. Reaching KATHERINE_E_NOMEM
    # means exhausting an allocation this test would have to make enormous,
    # which trades a reliable suite for one more asserted class.
    tap.comment('MemoryError is not provoked: see check_error_mapping()')

def run_checks(tap, katherine, device):
    check_enums(tap, katherine)
    check_enum_rendering(tap, katherine)
    check_readout_peripherals(tap, katherine)
    check_discovery(tap, katherine)
    check_usb_discovery(tap, katherine)
    check_module_surface(tap, katherine)
    check_timestamp_functions(tap, katherine)
    check_clock_helpers(tap, katherine)
    check_device_declaration(tap, katherine)
    check_pixel_types(tap, katherine)
    check_error_mapping(tap, katherine)

    tap.check_eq('get_chip_id() reports the expected identifier', device.get_chip_id(), EXPECTED_CHIP_ID)

    # Two calls nothing could reach from Python until they were bound.
    # set_token_count() is deliberately left out of configure(), so this is
    # the only way to choose it; verify_config() is second-generation only
    # and the emulator presents a first-generation readout, which is exactly
    # the refusal a caller would otherwise meet unexplained.
    device.set_token_count(8)
    tap.check('set_token_count() is accepted', True)

    refused = False
    try:
        device.verify_config()
    except Exception:
        refused = True
    tap.check('verify_config() is refused on a first-generation readout', refused)

    check = katherine.Tpx3ConfigCheck()
    tap.check('Tpx3ConfigCheck renders', repr(check).startswith('config_check{'))

    readout = device.get_readout_status()
    tap.check_eq('readout status: hw_type', readout.hw_type, EXPECTED_HW_TYPE)
    tap.check_eq('readout status: hw_revision', readout.hw_revision, EXPECTED_HW_REVISION)
    tap.check_eq('readout status: hw_serial_number', readout.hw_serial_number, EXPECTED_SERIAL)
    tap.check_eq('readout status: fw_version', readout.fw_version, EXPECTED_FW_VERSION)

    comm = device.get_comm_status()
    tap.check_eq('comm status: comm_lines_mask', comm.comm_lines_mask, EXPECTED_COMM_LINES)
    tap.check_eq('comm status: data_rate', comm.data_rate, EXPECTED_DATA_RATE)
    tap.check('comm status: chip_count', comm.chip_count > 0)

    tap.check_eq('readout temperature', device.get_readout_temperature(), EXPECTED_READOUT_T)
    tap.check_eq('sensor temperature', device.get_sensor_temperature(), EXPECTED_SENSOR_T)
    tap.check_eq('ADC channel %d voltage' % ADC_CHANNEL, device.get_adc_voltage(ADC_CHANNEL), EXPECTED_ADC_VOLTAGE)

    try:
        device.perform_digital_test()
        digital_test_ok = True
    except KATHERINE_ERRORS:
        digital_test_ok = False
    tap.check('digital test passes', digital_test_ok)

    version_str_ok = isinstance(katherine.__version__, str) and len(katherine.__version__) > 0
    tap.check('__version__ is a non-empty string', version_str_ok)

    project_version = find_project_version(os.path.abspath(__file__))
    if project_version is not None:
        tap.check_eq('__version__ matches the CMake project version', katherine.__version__, project_version)
    else:
        tap.comment("__version__ = '%s' (CMake project version not accessible from here, skipping the comparison)"
            % katherine.__version__)

    version_int_ok = isinstance(katherine.version(), int) and katherine.version() > 0
    tap.check('version() returns a positive integer', version_int_ok)

    check_reprs(tap, katherine, device)

    acq, probe = run_acquisition(katherine, device)

    tap.check('acquisition state is SUCCEEDED', acq.state == katherine.AcquisitionState.SUCCEEDED)
    tap.check_eq('acquisition completed 1 frame', acq.completed_frames, 1)
    tap.check_eq('acquisition dropped no measurement data', acq.dropped_measurement_data, 0)
    tap.check_eq('acquisition delivered all hits of the frame', probe.hits_delivered, HITS_PER_FRAME)

    tap.check('frame_ended fired with frame info', probe.frame_info is not None)
    if probe.frame_info is not None:
        info = probe.frame_info
        tap.check('frame reported completed', info.completed)
        tap.check_eq('frame reported sent_pixels', info.sent_pixels, HITS_PER_FRAME)
        tap.check_eq('frame reported received_pixels', info.received_pixels, HITS_PER_FRAME)
        tap.check_eq('frame reported lost_pixels', info.lost_pixels, LOST_PER_FRAME)
        tap.check_eq('frame end_time - start_time equals the configured shutter in ticks',
            info.end_time - info.start_time, EXPECTED_FRAME_TICKS)

    del acq


def main():
    if len(sys.argv) < 2:
        print('usage: %s <path-to-ksim> [module-dir]' % sys.argv[0], file=sys.stderr)
        return 2

    ksim_path = sys.argv[1]
    module_dir = sys.argv[2] if len(sys.argv) > 2 else os.environ.get('KATHERINE_PYTHON_MODULE_DIR')
    if module_dir:
        sys.path.insert(0, module_dir)

    tap = Tap()

    try:
        import katherine
    except ImportError as e:
        return skip('cannot import the katherine module: %s' % e)

    try:
        ksim = subprocess.Popen([
            ksim_path,
            '--listen', KSIM_LISTEN_ADDRESS,
            '--seed', str(KSIM_SEED),
            '--hits-per-frame', str(HITS_PER_FRAME),
            '--lost-per-frame', str(LOST_PER_FRAME),
            '--pattern', 'hot-column',
            '--quiet',
        ])
    except OSError as e:
        return skip("cannot spawn '%s': %s" % (ksim_path, e))

    try:
        device, reason = wait_ready(ksim, katherine)
        if device is None:
            return skip(reason)

        try:
            try:
                run_checks(tap, katherine, device)
            except Exception as e:
                tap.check('unexpected exception during checks', False, '%s: %s' % (type(e).__name__, e))
        finally:
            del device
    finally:
        stop_ksim(ksim)

    print('1..%d' % tap.count)
    sys.stdout.flush()
    return 1 if tap.failed else 0


if __name__ == '__main__':
    sys.exit(main())
