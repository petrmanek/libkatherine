#!/usr/bin/env python3
"""Compare libkatherine benchmark JSON-line output against a baseline.

Reads the {"bench": ..., "value": ..., "unit": ...} lines produced by the
c/benchmarks/ executables (a line that is not valid JSON, e.g. a "# ..."
comment, is silently skipped), and either records them as a new baseline or
compares them against one already recorded.

Usage:
    bench_compare.py --record BASELINE.json [FILE ...]
    bench_compare.py --against BASELINE.json [--tolerance FRACTION] [--ratios] [FILE ...]

With no FILE arguments, input is read from stdin.

--record stores every metric's value and unit, plus the ratio of every
decode_<variant> metric to the decode_memcpy reference in the same input (the
ratios bench_decode.c's --ratios mode below compares against): recording
both up front means a later --against needs nothing but the one baseline
file, in either mode.

--against compares each metric present in the baseline:
  - in absolute mode (the default), a metric's own recorded value;
  - with --ratios, its decode_<variant>/decode_memcpy ratio instead, which
    stays comparable across CI runners even though the absolute Mhit/s
    figures those ratios are built from do not.

A metric that falls below (1 - tolerance) times its baseline counterpart is
a regression; the command then exits nonzero and lists every offender. A
metric that improved on its baseline is reported but never fails the run.

@author Petr Mánek
@date 24.8.26

@copyright Copyright (c) 2018 Petr Mánek.
This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".

SPDX-License-Identifier: MIT
"""

import argparse
import json
import sys

MEMCPY_REF = "decode_memcpy"


def read_metrics(paths):
    """Reads JSON lines from `paths` (or from stdin if empty), returning a
    dict of bench name -> (value, unit). A line that is not a JSON object,
    or lacks "bench"/"value", is skipped rather than treated as an error:
    the benchmarks interleave informational "# ..." comment lines with the
    JSON metrics that matter here. A name repeated across the input keeps
    its last occurrence."""
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
        metrics[obj["bench"]] = (float(obj["value"]), obj.get("unit", ""))

    return metrics


def compute_ratios(metrics):
    """Ratio of every decode_<variant> metric to the decode_memcpy reference
    within the same metric set. Empty if the set carries no usable
    reference."""
    if MEMCPY_REF not in metrics:
        return {}
    memcpy_value = metrics[MEMCPY_REF][0]
    if memcpy_value <= 0:
        return {}

    return {
        name: value / memcpy_value
        for name, (value, _unit) in metrics.items()
        if name.startswith("decode_") and name != MEMCPY_REF
    }


def cmd_record(args):
    metrics = read_metrics(args.files)
    if not metrics:
        print("bench_compare: no metrics read from input", file=sys.stderr)
        return 1

    baseline = {
        "metrics": {name: {"value": value, "unit": unit} for name, (value, unit) in metrics.items()},
        "ratios": compute_ratios(metrics),
    }

    with open(args.record, "w") as f:
        json.dump(baseline, f, indent=2, sort_keys=True)
        f.write("\n")

    for name in sorted(metrics):
        value, unit = metrics[name]
        print(f"recorded {name} = {value:.4g} {unit}")
    print(f"bench_compare: wrote {len(metrics)} metric(s) to {args.record}")
    return 0


def cmd_against(args):
    metrics = read_metrics(args.files)

    with open(args.against, "r") as f:
        baseline = json.load(f)

    if args.ratios:
        current = compute_ratios(metrics)
        base = baseline.get("ratios", {})
        label = "ratio"
    else:
        current = {name: value for name, (value, _unit) in metrics.items()}
        base = {name: entry["value"] for name, entry in baseline.get("metrics", {}).items()}
        label = "value"

    if not base:
        print(f"bench_compare: baseline has no {label}s to compare against", file=sys.stderr)
        return 1

    threshold = 1.0 - args.tolerance
    failures = []

    for name in sorted(base):
        ref = base[name]
        if name not in current:
            print(f"MISSING   {name}: no {label} in input (baseline {ref:.4g})")
            failures.append(name)
            continue

        cur = current[name]
        change = (cur - ref) / ref if ref != 0 else float("inf") if cur != 0 else 0.0

        if cur < ref * threshold:
            status = "FAIL"
            failures.append(name)
        elif cur > ref:
            status = "IMPROVED"
        else:
            status = "ok"

        print(f"{status:9s} {name}: {label} {cur:.4g} vs baseline {ref:.4g} ({change:+.1%})")

    for name in sorted(set(current) - set(base)):
        print(f"EXTRA     {name}: {label} {current[name]:.4g} (not in baseline)")

    if failures:
        pct = f"{args.tolerance:.0%}"
        print(
            f"bench_compare: {len(failures)} metric(s) regressed beyond -{pct}: {', '.join(sorted(failures))}",
            file=sys.stderr,
        )
        return 1

    print(f"bench_compare: all {label}s within tolerance ({args.tolerance:.0%})")
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
