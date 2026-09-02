# tools/bench

Phase 3.5 measurement harness. See [docs/BENCH.md](../../docs/BENCH.md) for what
each number means and the scope procedure.

`bench.py` reuses `tools/pc_sender/` (`proto.py`, `open_box`, `wave_sine`) —
same `pip install pyusb` + libusb backend.

## Software tests (no external hardware)

```bash
python3 bench.py --all --out ../../docs/BENCH-results.md
```

| flag | measures |
|---|---|
| `--throughput` | sample-rate sweep (`--rates`, `--dwell`); highest `clean ✅` row = real ceiling |
| `--overload` | blast the pipe unpaced → does it drop cleanly, stay alive, no corruption |
| `--failsafe` | last-frame → `failsafe_trips` bump, ×5, min/mean/max |
| `--rtt` | PING→PONG over EP2, ×200, percentiles (USB + one box loop + USB) |
| `--usb-write` | `dev.write()` cost for a 12-byte frame, ×500 |

Writes a dated markdown report (with a copy of the scope procedure) to `--out`,
or to stdout.

## Scope session

```bash
# firmware built with BENCH_GPIO_TRACE 1
python3 bench.py --latency-probe            # 1 kHz, 1 samp/frame, 3 ms lookahead
python3 bench.py --latency-probe --lookahead-ms 40   # compare
```

Probe PA4 (per frame) / PA10 (per actuator update) / DRV2605 output. Fill the
table in `docs/BENCH.md` §2.
