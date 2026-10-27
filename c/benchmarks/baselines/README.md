# Benchmark baselines

One file per machine, written by `tools/bench_compare.py --record` and read
by `--against`. A file records, per metric, the **median** of several runs
with the spread around it -- not one run's number.

## These are a record, not a gate

Nothing in CI compares against them, and nothing should until a baseline has
been taken on a machine that was doing nothing else. The spread recorded
here is what that decision rests on, and it is the reason `--against`
widens a metric's tolerance to `--sigma` relative standard deviations of the
spread its baseline recorded.

## davos.json

| | |
|---|---|
| machine | davos -- Intel i7-8565U (4 cores / 8 threads), `powersave` governor |
| recorded | 2026-10-26, 15 metrics, 7 runs each |
| build | Release, the tree at the commit that added this file |
| spread | 0.84 % to 6.9 % relative standard deviation, by metric |

**The machine was not quiet.** A VirtualBox guest was holding about 37 % of
a core throughout, and a browser about 22 %. Compared with a 7-run
distribution taken on 2026-10-25:

- the **decoders moved between -6.2 % and +4.3 %**, which is inside their
  own spread -- the decode paths did not change;
- **`decode_memcpy` fell 14 %** and every UDP metric fell about 12 %.

`decode_memcpy` copies and decodes nothing; it is the control. It moving by
14 % while the decoders held is the machine moving, not the library.

## The ratios are not the machine-independent quantity they were taken for

`--ratios` divides each `decode_<variant>` by `decode_memcpy` from the same
run, on the reasoning that the quotient survives a change of machine. The
two numbers do not scale together:

| | 2026-10-25 | 2026-10-26 |
|---|---|---|
| `decode_toa_tot` | 518.5 Mhit/s | 499.9 Mhit/s (-3.6 %) |
| `decode_memcpy` | 2171.1 Mhit/s | 1867.6 Mhit/s (-14.0 %) |
| ratio | 0.2388 | 0.2677 (**+12.1 %**) |

Every ratio rose 9 % to 21 % on a night when the decoders themselves barely
moved, because the denominator fell. `decode_memcpy` is bound by memory
bandwidth and the decoders by core throughput, and contention does not touch
the two equally.

Per-run ratios are also **noisier than the absolute numbers they are built
from** -- 4.8 % to 10.5 % relative standard deviation against 0.84 % to
6.9 % -- because the reference's noise adds to the variant's rather than
cancelling it.

So `--ratios` still removes a *systematic* difference between machines, and
is the only thing that can; it does not remove an *episodic* one, and it
costs precision. A CI gate built on it would have to be looser than the
absolute one, not tighter.
