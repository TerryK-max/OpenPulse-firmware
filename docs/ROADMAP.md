# Roadmap — implementation tickets

Read [README.md](README.md), [ARCHITECTURE.md](ARCHITECTURE.md),
[PROTOCOL.md](PROTOCOL.md) first.

Phases are ordered by dependency. Do them in order. Each ticket lists **files**,
**do**, and **acceptance** (what "done" means — remember you cannot test on
hardware, so most acceptance is *compiles clean + matches the docs + user
confirms on bench* where marked 🔬).

Status legend: `[ ]` not started · `[~]` in progress · `[x]` done.
Keep this file updated as you complete tickets.

---

## Phase 0 — Refactor the bring-up into modules (no behaviour change)  —  DONE (pending 🔬)

Goal: turn `src/Main.c` into the [ARCHITECTURE.md](ARCHITECTURE.md) tree, byte-
for-byte identical behaviour (same boot log, same sweep, same demo). Pure code
motion + a build-system change. Completed 2026-08-31; awaits an on-bench
confirmation that the log/sweep/demo output is unchanged.

- `[x]` **0.1 Build system.** Added top-level **`Makefile`** (`make`, `make
  check`, `make clean`, `make flash`) that mirrors the MRS flags and writes to
  `build/`. MRS keeps working unchanged — its `.cproject` `sourceEntries` makes
  the whole project root a recursive source path (only `EVT` + some CH59x files
  excluded), so the new `src/**` folders are auto-discovered on the next MRS
  build; the stale `obj/src/Main.*` were removed. `make` build: FLASH 16.1 KB,
  RAM 3.4 KB, zero warnings at `-Wall -Wextra` on all `src/` TUs.
- `[x]` **0.2 `src/config.h`.** All `#define`s moved out of `Main.c`, grouped,
  with `[RUNTIME]` / `[MEASURED]` tags. `struct config` itself is deferred to
  Phase 1.3 where it is actually wired (kept Phase 0 zero-risk).
- `[x]` **0.3 `src/board/`.** `board_init()` = clock + `RB_PIN_DEBUG_EN` clear.
  `board/time.h` = thin `time_delay_ms/us` / `time_sysclock_hz` aliases. The
  free-running ms/us clock is deferred to **Phase 1.2** (it needs deliberate
  SysTick ownership; `mDelaymS` may use SysTick and Phase 0 must not disturb it).
- `[x]` **0.4 `src/i2c/i2c_master`.** Generic `i2c_write(addr8,reg,buf,len)` /
  `i2c_read_reg(addr8,reg,*val)` / `i2c_write_reg` + `i2c_master_init(hz)` /
  `i2c_master_recover()`. Status enum renamed `drv_status_t`→`i2c_status_t`
  (`DRV_OK`→`I2C_OK` …). `i2c_write_async()` noted in the header, not stubbed.
- `[x]` **0.5 `src/drv2605/`.** `drv2605_regs.h` (datasheet §-cited).
  `drv2605.{c,h}` = pure reg r/w wrappers + `wait_go_clear` + `devid_str`.
  `drv2605_lra.{c,h}` = pure open-loop primitives (`hz_to_ol_period`,
  `olp_to_mhz`, `effective_olp`, `lra_set_open_loop`, `rtp_open/level/close`).
  `drv2605_bench.{c,h}` = the verbose harness (probe/reset/autocal/play_effect/
  measure_resonance/sweep/max_drive_test), whole file behind
  `#if DRV2605_BENCH_TOOLS`, allowed to depend on `log/`.
- `[x]` **0.6 `src/haptic/haptic_patterns`.** `haptic_pulse()` (was
  `drv2605_lra_pulse`), `haptic_demo_simracing()` (was `drv2605_demo_simracing`).
- `[x]` **0.7 `src/log/log`.** `log_init` / `log_is_connected` / `log_puts` /
  `log_printf` forward to `usb_log.c` (output unchanged). `LOG_ERR/WARN/INFO/DBG`
  enum + `LOGE/W/I/D` macros + compile-time `LOG_MIN_LEVEL`. The runtime
  `log_set_sink()` + ring is **Phase 3.3**.
- `[~]` **0.8 `src/main.c`.** `Main.c`→`main.c`; body now calls the modules,
  same flow, `#error` if `DRV2605_BENCH_TOOLS` is off (renderer = Phase 1).
  **Acceptance 🔬 — OPEN**: user flashes, confirms the USB boot log, the STEP-6
  sweep, and the sim-racing demo are unchanged from before the refactor.

---

## Phase 1 — Haptic engine, FIFO, tick, failsafe  —  DONE, bench-confirmed

Goal: a running renderer fed by a *local* generator, with the real timing model.
Completed 2026-08-31; builds 0 warnings. `DRV2605_BENCH_TOOLS` flipped to **0**
in `config.h` — `make` now builds the renderer; the bench harness is one
`#define` away.

**Bench result (2026-08-31):** 34 s of `[R]` lines — `ovr=0 unr=0 i2c_err=0
fs=0`, `played` advancing at exactly 1000/s, `fifo` steady at the 16-sample
lookahead, `backlog` max = 4 (a diagnostic-logging burst, not the pipeline),
`STAT=0xE4` (no OVER_TEMP / OC), VBAT ~5.1–5.25 V. Pipeline validated.

- `[x]` **1.1 `src/haptic/haptic_fifo.h`.** SPSC `int8` ring, power-of-two
  (`HAPTIC_FIFO_CAP`), header-only, compiler-barrier discipline, overrun/underrun
  return codes. Host unit test deferred to Phase 2.5 (`tools/`).
- `[x]` **1.2 SysTick tick.** `src/board/time.c` — confirmed `mDelayuS`/`mDelaymS`
  are pure NOP loops (not SysTick), so SysTick is free. `time_tick_start(hz)` +
  `SysTick_Handler` (`s_ticks++` only) + `time_ticks()` / `time_now_ms()`. The
  main loop consumes `now - serviced` ticks per iteration, capped at 8, and
  records `stats.tick_backlog_max`.
- `[x]` **1.3 `src/haptic/haptic_engine`.** `haptic_tick()`: mode-change ramp
  through 0; SAMPLES pop (or underrun decay); ENVELOPE ramp; LOCAL_TEST →
  `haptic_pattern_simracing()`; `amp_max` clamp; `drv2605_set_amplitude()` (with
  the unchanged-value write-skip cache in `drv2605_lra.c`). Owns
  `haptic_config_t` (seeded from `config.h`), `haptic_stats_t`, `last_data_ms`.
  `haptic_apply_config()` staged, applied in `haptic_service()`.
- `[x]` **1.4 Failsafe.** `haptic_service()` in the main loop: `mode ∈
  {SAMPLES,ENVELOPE}` && `time_now_ms() - last_data_ms > failsafe_ms` → drain
  FIFO, envelope target 0, `stats.failsafe_trips++`; the tick's decay ramps the
  output to 0. Test knob: `HAPTIC_TEST_STOP_PROD_S` (config.h).
- `[x]` **1.5 Measure real I²C write time.** Renderer boot times 2000 RTP
  writes: **[MEASURED] ~126 µs/byte → max ~7.9 kHz** (recorded in
  [HARDWARE.md](HARDWARE.md) §1.2). `HAPTIC_SAMPLE_RATE_HZ = 1000` confirmed —
  headroom ~7.9× (≥ 3× target met); 2 kHz also fine.
  **Acceptance — MET (2026-08-31)**: on-bench `[R]` lines show `ovr=0 unr=0`,
  `backlog` max 4 (logging burst), `played` at 1000/s, no thermal/OC. See the
  bench result note above.

---

## Phase 2 — Link layer + protocol  —  DONE (2026-09-01)

Goal: bytes-in → validated commands, commands-out → bytes, transport-agnostic.
Builds 0 warnings (FLASH 15.0 KB, RAM 4.3 KB). Host harness: **83 checks, 0
failures** (`make test`). `link/` proven free of `transport/` / `usb/` includes.

- `[x]` **2.1 `src/link/proto.h`** — per [PROTOCOL.md](PROTOCOL.md) §8 plus
  `fault_msg`, the config field-range constants (`PROTO_RATE_MIN` …), frame
  offset macros and `PROTO_RESYNC_GAP`. `_Static_assert` on every struct size
  (2/12/5/40/5). Byte-identical copy at `tools/proto/proto.h`; `make test`
  `diff`s the two.
- `[x]` **2.2 `src/util/crc.{c,h}`** — CRC-8/SMBUS bitwise. `proto_crc8()` is a
  thin alias so there is one implementation. Check value `"123456789"` → `0xF4`
  asserted in the harness.
- `[x]` **2.3 `src/link/link.{c,h}`.** `link_rx()`: LEN + CRC + LEN-vs-delivered
  check (failures → `crc_err`), RESYNC-before-SEQ, gap accounting (`gap 1..15`
  → `seq_gap_frames` + DATA loss-policy fill; `gap ≥ 16` → `do_resync`),
  TYPE dispatch. `link_tx_pong/status/log/fault` assemble a frame (own SEQ
  counter) and call an injected `link_send_fn` (NULL-safe). Version gate
  (`link_data_blocked`). No `board/time` dependency — uses `haptic_now_ms()`.
- `[x]` **2.4 `src/link/link_control.{c,h}`.** `CTRL_SET_CONFIG` (version →
  `FAULT_VERSION` + block data plane; then every field vs its range → all-or-
  nothing, `FAULT_BAD_CONFIG`; success → `haptic_apply_config` staged +
  `STATUS_REP`). `CTRL_SET_MODE` (range → `FAULT_BAD_MODE`; refused to non-idle
  while version-blocked). `CTRL_PING` → `PONG`. `STATUS_REQ` → `STATUS_REP`.
  `RESYNC` handled in `link.c`. Mode changes go through `haptic_set_mode()`
  (ramps through 0).
- `[x]` **2.5 Host test harness** — `tools/test/` (`make test` from repo root).
  `mock_engine.c` doubles `haptic_engine.h` (real `haptic_fifo.h`); `test_link.c`
  drives `link_rx()` with crafted frames + a capturing sink. Covers: CRC vector,
  struct sizes, good DATA, bad CRC, short/oversize/LEN-mismatch, SEQ gap
  (zero-fill + hold-last), big-gap resync, explicit RESYNC + SEQ reseat, PING→
  PONG, STATUS_REQ→STATUS_REP (field decode), SET_CONFIG ok / bad-field /
  bad-reserved / short, version mismatch (+ DATA blocked + mode refused +
  recovery), SET_MODE ok/bad, ENVELOPE ok/malformed, unknown type → LOG(WARN).
  **Acceptance — MET**: harness green; `make test` runs the isolation grep and
  the `proto.h` `diff`.

**On-device integration:** `HAPTIC_TEST_VIA_LINK=1` (default) routes the
renderer's generator through the real link layer — frames `DATA_SAMPLES`, calls
`link_rx()`, boot `PING`/`SET_CONFIG`/`SET_MODE` handshake, periodic
`STATUS_REQ`. Phase 3 kept this as the **no-PC self-test** (suspended the moment
`transport_usb.host_active()`). 🔬 open with the Phase 3 bench (below).

---

## Phase 3 — USB transport (composite device)  —  3.1/3.2/3.4 DONE (2026-09-02)

Goal: real data over USB; CDC kept for logs. Builds 0 warnings (FLASH 16.3 KB,
RAM 6.3 KB / 12). `make test` still 83/0.

- `[x]` **3.1 `src/usb/usb_device`.** The bench-proven single-interface CDC
  device (old `src/usb_log.c`) became a **composite** — enumeration SM + EP0
  descriptor engine kept as-is, descriptors rebuilt, EP2 added:
  - Device `0xEF/0x02/0x01` (IAD), bcdUSB 2.00, `USB_VID:USB_PID` 1A86:5730.
  - Config (98 B, 3 interfaces): IAD → iface 0 CDC control (EP4 IN int 8) →
    iface 1 CDC data (EP1 OUT/IN bulk 64) → iface 2 vendor `0xFF` (EP2 OUT/IN
    bulk 64). Descriptor bytes validated structurally.
  - `usb_cdc.h`: `usb_cdc_write/printf/connected` (EP1 IN log ring, 1 KB).
  - `usb_vendor.h`: `usb_vendor_set_rx` / `_poll` / `_send` / `_host_active` /
    `_stats`. RX: the ISR stages each EP2-OUT packet in a ring (O(1), no parse);
    `usb_vendor_poll()` feeds them to the callback from the **main loop**. TX:
    `usb_vendor_send()` queues, the ISR drains onto EP2 IN. EP2 mirrors the
    proven EP1 CTRL discipline (`AUTO_TOG`, R_TOG flip on OUT, NAK on IN done).
  - `src/usb_log.{c,h}` deleted; `log/` facade now forwards to `usb_cdc`.
  - **MS OS 2.0 / WinUSB auto-bind deferred to 3.6** — no WCH reference for it,
    untestable here, and Windows-only (dev is on macOS, where libusb claims a
    class-`0xFF` interface with no OS descriptors).
- `[x]` **3.2 `src/transport/{transport.h, transport_usb}`.** `transport_usb`
  (a `transport_t` vtable) binds `usb_vendor` ↔ `link`: `init()` =
  `link_init(usb_send_adapter)` + `usb_vendor_set_rx(link_rx)`; `poll()` =
  `usb_vendor_poll()`; `host_active()`. `link_rx` runs from the main loop (not
  the ISR), so its `link_tx_*` replies queue cleanly. `TRANSPORT_USB_TX_TRACE`
  (config.h, default 1) decodes each outbound frame to the CDC log.
- `[~]` **3.3 Logging split.** DEFERRED — CDC stays the only `log` sink for now.
  `log.h` already has the level enum; `log_set_sink()` (+ `TYPE_LOG` frame sink
  + PA7 UART sink) is a clean follow-up.
- `[x]` **3.4 `tools/pc_sender/`.** `proto.py` (hand-maintained mirror of
  `proto.h`: framing, CRC-8, struct pack — self-tested) + `openpulse_send.py`
  (pyusb): handshake → stream `DATA_SAMPLES` (`--wave sim|sine`, paced to real
  time) → poll `STATUS_REQ` at 10 Hz → dashboard; background reader thread for
  PONG/STATUS/LOG/FAULT. `--list`, Ctrl-C → `SET_MODE(IDLE)`.
- `[~]` **3.5 Bench 🔬.** **Enumeration + end-to-end path confirmed on bench
  (2026-09-02):** composite device comes up, `openpulse_send.py --wave sim`
  drives a **smooth continuous rumble**, `crc=0 bad=0 ovr=0`, `played` ≈ 1 kHz,
  Ctrl-C silences it with no perceptible latency, CDC `[R]` keeps printing.
  Fixed during bring-up: PC handshake sends `RESYNC` first (was a spurious
  startup `gap`); PC keeps a 40 ms lookahead + primes the FIFO (was streaming
  at exactly 1× → jitter underruns); **`haptic_set_mode()` now flushes the FIFO
  at request time, not when the ramp completes** — otherwise a PC priming right
  behind its `SET_MODE(SAMPLES)` had its samples discarded (FIFO stabilised
  low / underran on any run that caught the box in `IDLE`).
  **Tooling in place** (2026-09-02): `tools/bench/bench.py` (software:
  throughput sweep, overload, failsafe latency, PING/PONG RTT, USB-write time →
  `docs/BENCH-results.md`); `BENCH_GPIO_TRACE` build flag +
  `src/bench/bench_trace.h` toggling PA4 (per frame) / PA10 (per render tick)
  for the scope; `bench.py --latency-probe` drives a scope-friendly stream;
  `docs/BENCH.md` has the full procedure + a results table to fill.
  **To do (at the bench, needs a scope — user has school access in the coming
  days)**: run `bench.py --all`; then the analog measurements (§2 of BENCH.md).
  Residual (cosmetic): `tick_backlog_max` ~16 at connect (one transient, FIFO
  absorbs it — lower it with `TRANSPORT_USB_TX_TRACE=0`); `seq_gap` ~2 once at
  handover.

- `[ ]` **3.6 MS OS 2.0 descriptors (Windows WinUSB auto-bind).** BOS + MS OS
  2.0 descriptor set (compatible-ID `WINUSB` + a DeviceInterfaceGUID) on
  interface 2, served via a vendor control request. Needs a Windows machine to
  verify. Until then Windows users need a one-line Zadig/`libusbK` bind.

---

## Phase 4 — 2.4 GHz transport (needs the next board revision)

Goal: same link layer over the radio; a USB↔RF dongle.

- `[ ]` **4.1 `src/rf/rf_link`.** Wrap the WCH proprietary 2.4 GHz mode
  (`EVT/EXAM/RF/RF_Basic`). Fixed pipe/address, one channel (or a tiny hop set),
  RX DMA → callback.
- `[ ]` **4.2 `src/transport/transport_rf`.** RX packet → `link_rx()`; `link`
  outbound (STATUS/LOG/PONG) → RF TX back-channel. Bigger FIFO cushion
  (`config.h`), zero-fill loss policy default, optional double-send.
- `[ ]` **4.3 `targets/dongle/`.** Separate firmware build: USB vendor bulk from
  the PC ↔ RF TX to the box, and RF RX from the box ↔ USB bulk IN to the PC.
  Reference: `EVT/EXAM/RF/RF_UartDongle`. Shares `link`/`proto` (it can be a
  dumb byte pipe, or it can parse for stats — decide).
- `[ ]` **4.4 `log/` over RF.** When the box has no USB data link, `log` sink =
  `TYPE_LOG` frame over the RF back-channel, or the PA7 UART. No application
  code changes — only `log_set_sink()`.
- `[ ]` **4.5 Link-loss handling.** Define "RF link lost" (N missed expected
  frames / T ms silent) → immediate failsafe.
  **Acceptance 🔬**: box driven entirely over RF via the dongle; latency and
  loss characterised in `docs/BENCH.md`; pulling the dongle trips the failsafe.

---

## Phase 5 — (optional, needs hardware) PWM-input high-rate path

Only if 1–2 kHz over I²C proves insufficient in practice. Requires routing
DRV2605 `IN/TRIG` → PA7 (conflicts with the debug UART — pick one) or another
PWM/timer pin on a board revision.

- `[ ]` **5.1** DRV2605 `MODE=3` (PWM input), `N_PWM_ANALOG=0`. Config over I²C
  only.
- `[ ]` **5.2** MCU generates a PWM carrier (~20 kHz) on the `TMR`/PWM channel;
  DMA feeds the duty from the sample FIFO. Sink selected behind
  `drv2605_set_amplitude()` — nothing above `drv2605_lra` changes.
- `[ ]` **5.3** Raise `config.sample_rate_hz` limit; re-benchmark.

---

## Phase 6 — Production hardening

- `[ ]` **6.1 IWDG watchdog.** Kicked in the main loop; a stuck loop → reset →
  boot ramps from 0.
- `[ ]` **6.2 Config persistence.** `CTRL_SET_CONFIG` with the persist flag →
  write `struct config` to on-chip **DataFlash** (`EVT/EXAM/FLASH`), CRC'd, with
  a valid-marker; load at boot, fall back to `config.h` defaults if absent/bad.
- `[ ]` **6.3 Thermal foldback.** NTC on the actuator → ADC → `haptic_engine`
  scales `amp_max` down on a curve as temperature rises; `FAULT_DRV_OT` path for
  the chip's own OVER_TEMP. See [HARDWARE.md](HARDWARE.md) §4.
- `[ ]` **6.4 Fault/event ring in DataFlash.** Boot count, last reset reason,
  last N `FAULT`s / mode changes. Dumped via a `CTRL` command. Rate-limited;
  never written from the hot path.
- `[ ]` **6.5 Firmware update.** USB DFU or the WCH USB-IAP
  (`EVT/EXAM/IAP/USB_IAP`). Bootloader partition in `Ld/`.
- `[ ]` **6.6 Boot self-test report.** Probe result, DEVICE_ID, config source,
  a single confirmation pulse; surfaced in the first `STATUS_REP`.

---

## Deferred / open questions

- Redundant RF sends vs FEC — decide in Phase 4 from measured loss.
- Whether the dongle parses the protocol (for its own stats / a status LED) or
  stays a transparent pipe.
- Multi-actuator: **out of scope** (single actuator confirmed). If it ever
  returns, `proto.h` gains a channel byte and the FIFO becomes per-channel.
- PC-side stack/language for `tools/pc_sender` beyond the reference — the user
  will likely integrate with SimHub / a game telemetry source.
