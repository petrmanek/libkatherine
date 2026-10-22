#!/usr/bin/env python3
"""The three kdac implementations must agree byte for byte.

kdac exists in C, C++ and Python against one CLI, so --json output from the
three is the first real test the bindings have: a wrapper that converts a
value wrongly, or an enumeration whose order drifted, shows up here as a diff
rather than as a surprise at a readout.

Runs each implementation against ksim's second-generation profile, one after
another and never concurrently -- they bind the same local ports, so two at
once means one of them loses the readout's reply and reports silence.

An environment that cannot host the daemon, or a build without one of the
three, is reported as a skip (TAP "1..0 # SKIP <reason>", exit code 77),
mirroring python/tests/smoke.py. Every other failure is a real one.

Copyright (c) 2018 Petr Manek.
This software is distributed under the terms of the MIT License, copied
verbatim in the file "LICENSE".

SPDX-License-Identifier: MIT
"""

import os
import subprocess
import sys
import time

KSIM_LISTEN_ADDR = '127.0.0.2'
KSIM_PROFILE = 'gen2-tpx3'
KSIM_STOP_TIMEOUT_S = 5
READY_ATTEMPTS = 50
READY_SLEEP_S = 0.1
RUN_TIMEOUT_S = 60

# Every case is run through all three. The device-dependent ones all use
# --json, which is what the three are contracted to agree on; `list` is here
# in both forms because it is the one case that needs no readout at all.
CASES = [
    ['list'],
    ['list', '--json'],
    ['list', '--si'],
    ['monitor', '--json'],
    ['monitor', '--json', 'BandGap_Temp'],
    ['monitor', '--json', 'Vthreshold_fine'],
    ['scan', '--json', 'Vfbk', '0', '6'],
    ['scan', '--json', '--si', 'VTP_coarse', '0', '3'],
    ['set', '--si', 'Vthreshold_coarse', '7'],
    ['set', 'Vthreshold_fine', '388'],
    ['set'] + [str(i) for i in range(1, 19)],
    # Argument errors are part of the contract too: all three reject the same
    # inputs with the same words.
    ['set', 'Nonesuch', '1'],
    ['set', 'Vthreshold_coarse', '999'],
    ['scan'],
    ['monitor', '--json', 'Nonesuch'],
]

NEEDS_READOUT = {'monitor', 'scan', 'set'}


def skip(reason):
    print('1..0 # SKIP %s' % reason)
    sys.stdout.flush()
    return 77


def stop_ksim(proc):
    if proc.poll() is not None:
        return

    proc.terminate()
    try:
        proc.wait(timeout=KSIM_STOP_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def run(argv, env):
    """One implementation, one case. stdout and stderr are compared together:
    which stream a message goes to is part of what the three agree on."""
    done = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          timeout=RUN_TIMEOUT_S, env=env)
    return done.returncode, done.stdout


def wait_ready(ksim, c_kdac, env):
    """Waits until the C implementation can read the monitors, which is the
    same readiness the other two need."""
    for _ in range(READY_ATTEMPTS):
        code, _out = run([c_kdac, 'monitor', '--json', '--address', KSIM_LISTEN_ADDR], env)
        if code == 0:
            return None

        if ksim.poll() is not None:
            return ('ksim exited during startup: it could not bind %s (not aliased by default '
                    'on macOS), or could not be executed at all' % KSIM_LISTEN_ADDR)

        time.sleep(READY_SLEEP_S)

    return ('no answer from ksim at %s:1555 within %d attempts: the local ports may be bound by '
            'another process' % (KSIM_LISTEN_ADDR, READY_ATTEMPTS))


def main(argv):
    if len(argv) != 5:
        print('usage: kdac_parity.py <ksim> <kdac> <kdacxx> <pykatherine-dir>', file=sys.stderr)
        return 2

    ksim_path, c_kdac, cxx_kdac, module_dir = argv[1:5]
    py_kdac = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'examples', 'kdac.py')
    py_kdac = os.path.normpath(py_kdac)

    for path in (ksim_path, c_kdac, cxx_kdac, py_kdac):
        if not os.path.exists(path):
            return skip('%s was not built' % os.path.basename(path))

    env = dict(os.environ)
    env['PYTHONPATH'] = module_dir + os.pathsep + env.get('PYTHONPATH', '')

    ksim = subprocess.Popen(
        [ksim_path, '--profile', KSIM_PROFILE, '--listen', KSIM_LISTEN_ADDR, '--quiet'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    try:
        reason = wait_ready(ksim, c_kdac, env)
        if reason is not None:
            return skip(reason)

        print('1..%d' % len(CASES))
        failures = 0

        for n, case in enumerate(CASES, start=1):
            args = list(case)
            if case[0] in NEEDS_READOUT:
                args += ['--address', KSIM_LISTEN_ADDR]

            # Sequential by construction: the three bind the same local ports.
            results = []
            for name, argv_prefix in (
                    ('c', [c_kdac]),
                    ('c++', [cxx_kdac]),
                    ('python', [sys.executable, py_kdac])):
                results.append((name,) + run(argv_prefix + args, env))

            reference_name, reference_code, reference_out = results[0]
            disagreed = [r for r in results[1:]
                         if r[1] != reference_code or r[2] != reference_out]

            label = ' '.join(case)
            if not disagreed:
                print('ok %d - %s' % (n, label))
                continue

            failures += 1
            print('not ok %d - %s' % (n, label))
            for name, code, out in disagreed:
                print('# %s exited %d where %s exited %d' % (name, code, reference_name,
                                                             reference_code))
                print('# %s said: %r' % (name, out[:400]))
            print('# %s said: %r' % (reference_name, reference_out[:400]))

        return 1 if failures else 0
    finally:
        stop_ksim(ksim)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
