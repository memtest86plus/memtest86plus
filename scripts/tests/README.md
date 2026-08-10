These host regressions extract C functions from the current checkout on each
run. Run them from the repository root:

```
python3 scripts/tests/numa_scheduler.py
python3 scripts/tests/numa_maps.py
python3 scripts/tests/numa_chunks.py
python3 scripts/tests/numa_window_barrier.py
python3 scripts/tests/numa_resident_sweeps.py
python3 scripts/tests/numa_fallback.py
python3 scripts/tests/numa_accounting.py
python3 scripts/tests/numa_topology.py --sanitize
```

Use `CC` or the script's `--cc` argument to select a host compiler. A 64-bit host
and Python 3 are required; the barrier regression also uses POSIX threads.

The suites check scheduler termination and per-stage ranges, stable ownership
and CPU partitions, deterministic publication races, actual relocation copies
with own-address and bit-fade patterns, safe failure fallback, work accounting,
and reversible LoongArch address transforms. Hardware mapping, cache behavior,
CPU startup and instruction execution after relocation require target boot or
hardware validation.

The fallback suite records the inherited legacy NUMA_OFF alignment tail
omission. These fixes also leave the pre-existing Modulo 20 minimum-chunk
restriction in place; neither limitation is an exactly-once coverage claim.
