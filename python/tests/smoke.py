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

KSIM_LISTEN_ADDR = '127.0.0.2'
KSIM_SEED = 12345
HITS_PER_FRAME = 40
LOST_PER_FRAME = 3

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
            device = katherine.Device(KSIM_LISTEN_ADDR)
        except OSError as e:
            return None, 'cannot bind the local control/data ports 1555/1556: %s' % os.strerror(e.errno)

        try:
            chip_id = device.get_chip_id()
        except OSError:
            chip_id = None

        if chip_id == EXPECTED_CHIP_ID:
            return device, None

        del device

        if ksim.poll() is not None:
            return None, ('ksim exited during startup: it could not bind %s (not aliased by default on macOS), '
                           'or could not be executed at all' % KSIM_LISTEN_ADDR)

        time.sleep(READY_SLEEP_S)

    return None, ('no answer from ksim at %s:1555 within %d attempts: the local ports may be bound on the wildcard '
                  'address by another process, or the wildcard/specific same-port coexistence this needs may be '
                  'unsupported here (unproven on Windows)' % (KSIM_LISTEN_ADDR, READY_ATTEMPTS))


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
    contents, so the all-zero matrix a fresh PxConfig starts out with is
    uploaded and accepted like any other."""
    c = katherine.Config()

    c.bias_id = 0
    c.acq_time = SHORT_ACQ_TIME_NS
    c.no_frames = 1
    c.bias = 230

    c.start_trigger = katherine.Trigger(enabled=False)
    c.stop_trigger = katherine.Trigger(enabled=False)
    c.delayed_start = False

    c.gray_disable = False
    c.polarity_holes = False

    c.phase = katherine.Phase.PHASE_1
    c.freq = katherine.Freq.FREQ_40

    dacs = katherine.Dacs()
    dacs.Ibias_Preamp_ON = 128
    dacs.Ibias_Preamp_OFF = 8
    dacs.VPReamp_NCAS = 128
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

    c.pixel_config = katherine.PxConfig()

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
        device, katherine.MD_SIZE() * 4096, katherine.PxFastToaTot.RAW_SIZE() * 1024, 500, 10000)
    probe = make_probe(katherine)
    acq.observer = probe

    acq.begin(config, katherine.ReadoutType.DATA_DRIVEN, katherine.AcquisitionMode.TOA_TOT, True)
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

    tp = katherine.TestPulseConfig(enabled=True, digital_only=False, external=False, count=100, period=65, phase=0)
    tap.check_eq('TestPulseConfig repr matches the C golden byte-for-byte', repr(tp),
        'test_pulse_config{enabled: true, digital_only: false, external: false, count: 100, period: 65, phase: 0}')

    trig = katherine.Trigger(enabled=True, channel=3, use_falling_edge=False)
    tap.check_eq('Trigger repr matches the C golden byte-for-byte', repr(trig),
        'trigger{enabled: true, channel: 3, use_falling_edge: false}')

    px = katherine.PxConfig()
    tap.check_eq('PxConfig repr matches the C golden byte-for-byte (all-zero matrix)', repr(px),
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


def run_checks(tap, katherine, device):
    tap.check_eq('get_chip_id() reports the expected identifier', device.get_chip_id(), EXPECTED_CHIP_ID)

    readout = device.get_readout_status()
    tap.check_eq('readout status: hw_type', readout.hw_type, EXPECTED_HW_TYPE)
    tap.check_eq('readout status: hw_revision', readout.hw_revision, EXPECTED_HW_REVISION)
    tap.check_eq('readout status: hw_serial_number', readout.hw_serial_number, EXPECTED_SERIAL)
    tap.check_eq('readout status: fw_version', readout.fw_version, EXPECTED_FW_VERSION)

    comm = device.get_comm_status()
    tap.check_eq('comm status: comm_lines_mask', comm.comm_lines_mask, EXPECTED_COMM_LINES)
    tap.check_eq('comm status: data_rate', comm.data_rate, EXPECTED_DATA_RATE)
    tap.check('comm status: chip_detected', comm.chip_detected)

    tap.check_eq('readout temperature', device.get_readout_temperature(), EXPECTED_READOUT_T)
    tap.check_eq('sensor temperature', device.get_sensor_temperature(), EXPECTED_SENSOR_T)
    tap.check_eq('ADC channel %d voltage' % ADC_CHANNEL, device.get_adc_voltage(ADC_CHANNEL), EXPECTED_ADC_VOLTAGE)

    try:
        device.perform_digital_test()
        digital_test_ok = True
    except OSError:
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
            '--listen', KSIM_LISTEN_ADDR,
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
