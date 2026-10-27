#!/usr/bin/env python3
"""Compare libkatherine benchmark JSON-line output against a baseline.

Reads the {"bench": ..., "value": ..., "unit": ...} lines produced by the
c/benchmarks/ executables (a line that is not valid JSON, e.g. a "# ..."
comment, is silently skipped), and either records them as a new baseline or
compares them against one already recorded.

Usage:
    bench_compare.py --record BASELINE.json [FILE ...]
    bench_compare.py --against BASELINE.json [--tolerance FRACTION]
                     [--sigma N] [--ratios] [FILE ...]

With no FILE arguments, input is read from stdin.

A metric named more than once across the input is a repeated measurement,
not a correction: every occurrence is kept and the median is what gets
recorded or compared. This is what a baseline needs -- the run-to-run spread
on the machine these numbers come from is 1.2 % to 11 % depending on the
metric, so a baseline taken from one run records that run's luck and a gate
built on it fails on noise. Hand --record several runs of the same
benchmark.

--record stores every metric's median and unit, plus how many samples it saw
and, for more than one, the relative standard deviation and the range. It
stores the same for the ratio of every decode_<variant> metric to the
decode_memcpy reference (the ratios bench_decode.c's --ratios mode below
compares against), computed per run where the runs line up: recording both
up front means a later --against needs nothing but the one baseline file, in
either mode.

--against compares each metric present in the baseline:
  - in absolute mode (the default), a metric's own recorded value;
  - with --ratios, its decode_<variant>/decode_memcpy ratio instead, which
    removes a systematic difference between machines that the absolute
    Mhit/s figures carry. It does not remove an episodic one: the two
    halves do not scale together -- measured 2026-10-26, the decoders held
    while decode_memcpy fell 14 %, and every ratio rose 9 % to 21 % -- and
    a per-run ratio is noisier than either half, the reference's noise
    adding to the variant's. c/benchmarks/baselines/README.md has the
    numbers.

A metric that falls below (1 - tolerance) times its baseline counterpart is
a regression; the command then exits nonzero and lists every offender. A
metric that improved on its baseline is reported but never fails the run.
Where the baseline recorded a spread, the tolerance for that metric widens
to --sigma relative standard deviations if that is wider than --tolerance,
so a metric that is noisy by nature is not reported as a regression for
being noisy. decode_memcpy decodes nothing and is the control: it moving
while the decoders hold is the machine moving, not the library.

@author Petr Mánek
@date 24.8.26

@copyright Copyright (c) 2018 Petr Mánek.
This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".

SPDX-License-Identifier: MIT
"""

import argparse
import json
import statistics
import sys

MEMCPY_REF = "decode_memcpy"


def read_metrics(paths):
    """Reads JSON lines from `paths` (or from stdin if empty), returning a
    dict of bench name -> (samples, unit). A line that is not a JSON object,
    or lacks "bench"/"value", is skipped rather than treated as an error:
    the benchmarks interleave informational "# ..." comment lines with the
    JSON metrics that matter here. A name repeated across the input is a
    repeated measurement and every occurrence is kept, in input order, so
    that files given in run order stay aligned across metrics."""
    if paths:
        lines = []
        for path in paths:
            with open(path, "r") as f:
                lines.extend(f.readlines())
    else:
        lines = sys.stdin.readlines()

    metrics = {}
    for line in lines:
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except ValueError:
            continue
        if not isinstance(obj, dict) or "bench" not in obj or "value" not in obj:
            continue
        name = obj["bench"]
        samples, unit = metrics.setdefault(name, ([], obj.get("unit", "")))
        samples.append(float(obj["value"]))

    return metrics


def summarize(samples):
    """The median of `samples`, with the spread that says how much to trust
    it. The relative standard deviation is omitted for a single sample --
    not because it is zero, but because one run has no spread to report."""
    summary = {"value": statistics.median(samples), "n": len(samples)}
    if len(samples) > 1:
        mean = statistics.fmean(samples)
        sd = statistics.stdev(samples)
        summary["rsd_pct"] = round(100.0 * sd / mean, 2) if mean else 0.0
        summary["min"] = min(samples)
        summary["max"] = max(samples)
    return summary


def compute_ratios(metrics):
    """Ratio of every decode_<variant> metric to the decode_memcpy reference,
    as a list of samples per metric. Taken run by run where the metric has as
    many samples as the reference does -- which is what cancels a machine-wide
    excursion, since both halves of the ratio then come from the same run --
    and from the two medians otherwise. Empty if the set carries no usable
    reference."""
    if MEMCPY_REF not in metrics:
        return {}
    memcpy = metrics[MEMCPY_REF][0]
    if min(memcpy) <= 0:
        return {}

    ratios = {}
    for name, (samples, _unit) in metrics.items():
        if not name.startswith("decode_") or name == MEMCPY_REF:
            continue
        if len(samples) == len(memcpy):
            ratios[name] = [value / ref for value, ref in zip(samples, memcpy)]
        else:
            ratios[name] = [statistics.median(samples) / statistics.median(memcpy)]

    return ratios


def cmd_record(args):
    metrics = read_metrics(args.files)
    if not metrics:
        print("bench_compare: no metrics read from input", file=sys.stderr)
        return 1

    baseline = {
        "metrics": {name: dict(summarize(samples), unit=unit) for name, (samples, unit) in metrics.items()},
        "ratios": {name: summarize(samples) for name, samples in compute_ratios(metrics).items()},
    }

    with open(args.record, "w") as f:
        json.dump(baseline, f, indent=2, sort_keys=True)
        f.write("\n")

    for name in sorted(metrics):
        entry = baseline["metrics"][name]
        spread = f" +-{entry['rsd_pct']:.2g} % over {entry['n']} runs" if entry["n"] > 1 else " from one run"
        print(f"recorded {name} = {entry['value']:.4g} {entry['unit']}{spread}")
    print(f"bench_compare: wrote {len(metrics)} metric(s) to {args.record}")
    return 0


def baseline_entries(baseline, key):
    """The baseline's `key` section as name -> (value, rsd_pct). Accepts a
    bare number where a baseline recorded before the spread was kept has
    one, so an older file still compares."""
    entries = {}
    for name, entry in baseline.get(key, {}).items():
        if isinstance(entry, dict):
            entries[name] = (entry["value"], entry.get("rsd_pct"))
        else:
            entries[name] = (float(entry), None)
    return entries


def cmd_against(args):
    metrics = read_metrics(args.files)

    with open(args.against, "r") as f:
        baseline = json.load(f)

    if args.ratios:
        current = {name: statistics.median(samples) for name, samples in compute_ratios(metrics).items()}
        base = baseline_entries(baseline, "ratios")
        label = "ratio"
    else:
        current = {name: statistics.median(samples) for name, (samples, _unit) in metrics.items()}
        base = baseline_entries(baseline, "metrics")
        label = "value"

    if not base:
        print(f"bench_compare: baseline has no {label}s to compare against", file=sys.stderr)
        return 1

    failures = []
    widened = 0

    for name in sorted(base):
        ref, rsd_pct = base[name]
        if name not in current:
            print(f"MISSING   {name}: no {label} in input (baseline {ref:.4g})")
            failures.append(name)
            continue

        # A metric whose baseline recorded its own spread is allowed that
        # spread before it counts as a regression.
        tolerance = args.tolerance
        if rsd_pct is not None:
            tolerance = max(tolerance, args.sigma * rsd_pct / 100.0)
        if tolerance > args.tolerance:
            widened += 1

        cur = current[name]
        change = (cur - ref) / ref if ref != 0 else float("inf") if cur != 0 else 0.0

        if cur < ref * (1.0 - tolerance):
            status = "FAIL"
            failures.append(name)
        elif cur > ref:
            status = "IMPROVED"
        else:
            status = "ok"

        note = f", tolerance {tolerance:.0%}" if tolerance > args.tolerance else ""
        print(f"{status:9s} {name}: {label} {cur:.4g} vs baseline {ref:.4g} ({change:+.1%}{note})")

    for name in sorted(set(current) - set(base)):
        print(f"EXTRA     {name}: {label} {current[name]:.4g} (not in baseline)")

    if failures:
        pct = f"{args.tolerance:.0%}"
        print(
            f"bench_compare: {len(failures)} metric(s) regressed beyond -{pct}: {', '.join(sorted(failures))}",
            file=sys.stderr,
        )
        return 1

    note = f", {widened} widened to {args.sigma:g} sigma of its own spread" if widened else ""
    print(f"bench_compare: all {label}s within tolerance ({args.tolerance:.0%}{note})")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--record", metavar="BASELINE", help="record the input metrics into BASELINE")
    mode.add_argument("--against", metavar="BASELINE", help="compare the input metrics against BASELINE")
    parser.add_argument(
        "--tolerance",
        type=float,
        default=0.10,
        help="fractional regression tolerance for --against (default: 0.10)",
    )
    parser.add_argument(
        "--sigma",
        type=float,
        default=3.0,
        help="with --against, widen a metric's tolerance to this many relative standard "
        "deviations of the spread its baseline recorded, where that is wider (default: 3)",
    )
    parser.add_argument(
        "--ratios",
        action="store_true",
        help="with --against, compare decode_<variant>/decode_memcpy ratios instead of absolute values",
    )
    parser.add_argument("files", nargs="*", metavar="FILE", help="JSON-line input files (default: read stdin)")
    args = parser.parse_args()

    if args.ratios and not args.against:
        parser.error("--ratios only applies to --against")

    if args.record:
        return cmd_record(args)
    return cmd_against(args)


if __name__ == "__main__":
    sys.exit(main())
