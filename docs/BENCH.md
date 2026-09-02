# Bench — Phase 3.5 measurements

Read [ROADMAP.md](ROADMAP.md) §3.5 for *why* each number matters (throughput
ceiling → is Phase 5 needed; latency budget → how much is the buffer tax vs
mechanics; loss behaviour → validates "lossy but live" before RF; failsafe
timing → the thermal-safety number; all of it → the baseline the RF transport
is designed against).

Two halves:

| | tool | needs |
|---|---|---|
| **Software** — throughput, frame stress, overload, failsafe, round-trip, USB write time | `tools/bench/bench.py` | just pyusb |
| **Analog** — render jitter, rx→playout latency, actuator rise, failsafe→silence | scope / logic analyzer on the `BENCH_GPIO_TRACE` pins | scope + `BENCH_GPIO_TRACE 1` build |

---

## 1. Software bench

```bash
cd tools/bench
python3 bench.py --all --out ../../docs/BENCH-results.md      # ~2 min, overwrites the results file
# or individual: --throughput --overload --failsafe --rtt --usb-write
```

`bench.py --all` sweeps the sample rate (500…4000 Hz), blasts the pipe
unpaced to check graceful degradation, times the failsafe, and measures the
PING→PONG round-trip and the raw `dev.write()` cost. It writes a dated markdown
report (tables + a copy of §2 below) to `--out`.

Results live in **`docs/BENCH-results.md`** (regenerated, not hand-edited).
Not run yet on this machine.

**Reading the throughput sweep:** the highest rate row still marked `clean ✅`
(Δunr = Δovr = Δcrc = 0) is the real sustainable ceiling of the *whole* path
(USB rx + CRC + dispatch + FIFO + tick + I2C + STATUS overhead) — not just the
~7.9 kHz that the isolated I2C write allows ([HARDWARE.md](HARDWARE.md) §1.2).

---

## 2. Analog bench (scope / logic analyzer)

**Build:** set `BENCH_GPIO_TRACE 1` in `src/config.h`, `make`, flash. It claims
**PA4** and **PA10** (otherwise free — [HARDWARE.md](HARDWARE.md) §1.1):

| Pin | Toggles on | Marks |
|---|---|---|
| **PA4** | each `DATA_SAMPLES` frame handed to the link layer (`transport_usb.c` `tu_rx`) | "samples arrived" |
| **PA10** | each `drv2605_set_amplitude()` I2C write (`haptic_engine.c` `haptic_tick`) | "actuator command updated" = the render tick |

**Probe:** PA4, PA10, and a 3rd channel on a **DRV2605 OUT± / actuator
terminal**. Board GND common. If the analyzer decodes I2C, clip SCL/SDA (PA8/PA9)
too.

**Drive:** `python3 tools/bench/bench.py --latency-probe` — 1 kHz, 1 sample per
frame, 3 ms lookahead, so a fresh sample plays almost immediately (makes the
PA4→PA10 delay = pipeline latency, not buffer depth).

**Measure & record:**

1. **Render jitter** — PA10 period. Expect ~1.000 ms; note spread + worst
   outlier. Cross-check `tick_backlog_max` from the software run.
2. **rx → playout latency** — PA4 edge → next PA10 edge, at 3 ms lookahead.
   Expect ~1–4 ms. Re-run `--latency-probe` after bumping `--lookahead-ms` and
   watch it grow ≈1:1 with the buffer — that delta is the deliberate jitter
   margin, and tells us how low we can safely set the PC lookahead.
3. **I2C write duration** — PA10 edge → SCL/SDA settle. Cross-check the
   [MEASURED] ~126 µs.
4. **driver + LRA rise** — PA10 edge → actuator-terminal voltage change
   (electrical, ~instant) and, with an accelerometer taped on, → actuator
   physically moving (~10–20 ms — the dominant, unfixable term).
5. **failsafe → silence** — Ctrl-C `bench.py`, trigger on PA4 going idle,
   measure to the actuator drive collapsing to 0. Expect ≈ `failsafe_ms`
   (100) + `underrun_decay_ms` (20).

| measurement | value | notes |
|:--|--:|:--|
| PA10 period (render tick) | _±__ µs_ | |
| rx → playout @ 3 ms lookahead | _ ms_ | pipeline only |
| rx → playout @ 40 ms lookahead | _ ms_ | with the default safety buffer |
| I2C RTP write duration | _ µs_ | vs 126 µs |
| PA10 → actuator voltage | _ µs_ | electrical latency |
| PA10 → actuator motion | _ ms_ | accelerometer, or cite ~15 ms |
| failsafe: last frame → silence | _ ms_ | vs 100 + 20 |

---

## 3. Conclusions

_(fill in after the runs)_

- Sustainable sample rate with 0 loss: **___ Hz** → headroom vs the 1 kHz
  default is **___×**. Phase 5 PWM path needed? **___**
- Electronics latency (excl. mechanical): **___ ms**, of which buffer **___ ms**
  / pipeline **___ ms**. Safe minimum PC lookahead: **___ ms**.
- Overload degrades cleanly: **yes / no**.
- Failsafe real latency: **___ ms**.
- For Phase 4 (RF): need **___ ms** of FIFO to survive **___ %** loss at 1 kHz.
