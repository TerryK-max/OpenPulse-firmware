# Architecture

Read [README.md](README.md) first. This document describes the **target**
architecture. As of 2026-09-02 (Phases 0–3 done bar the bench check) layers 0–5
exist: `board/`, `i2c/`, `drv2605/`, `haptic/`, `link/`, `util/`, `log/`,
`usb/` (composite CDC + vendor bulk), `transport/` (`transport_usb`) and
`main.c`. Still to come: `rf/` + `transport_rf` (Phase 4), the `log_set_sink()`
split (Phase 3.3), MS OS 2.0 descriptors (Phase 3.6).

---

## 1. One-paragraph overview

The PC mixes every haptic effect into a single amplitude waveform and streams it
as small packets over **USB** (later also **2.4 GHz**). A transport driver hands
raw bytes to a **transport-agnostic link layer**, which validates framing/CRC/
sequence and pushes **samples** into a lock-free **FIFO** (or applies a control
message). A fixed-rate **tick** (SysTick ISR at `sample_rate_hz`) paces the main
loop, which pops one sample per tick and writes it to the **DRV2605L** over I²C
in **open-loop RTP** mode. A **failsafe** ramps the actuator to zero if telemetry
stops. Logging and status are strictly off the hot path.

```
        ┌──────────┐   USB bulk OUT / RF RX        ┌───────────────┐
  PC ─▶ │ transport│ ───────────────────────────▶  │  link (parse) │
        │  _usb /  │                               │  framing,CRC, │
        │  _rf     │ ◀───────── USB IN / RF TX ──── │  seq, dispatch│
        └──────────┘   (PONG / STATUS / LOG)       └───────┬───────┘
                                                          │ samples        control msgs
                                                          ▼                    │
                                              ┌────────────────────┐           ▼
                                              │ haptic_fifo (SPSC) │   ┌──────────────────┐
                                              └─────────┬──────────┘   │ config / mode /  │
                                                        │ 1 per tick   │ stats / failsafe │
   SysTick ISR @ sample_rate ── sets g_tick ──▶ main loop pops ──────▶ │ haptic_engine    │
                                                        │              └────────┬─────────┘
                                                        ▼                       ▼
                                              ┌────────────────────┐   ┌──────────────────┐
                                              │ drv2605 (I²C RTP)  │   │ drv2605 config   │
                                              └────────────────────┘   │ (reg 0x17/0x20…) │
                                                        │              └──────────────────┘
                                                        ▼
                                              Apple Taptic Engine (LRA)
```

---

## 2. Layers and modules

Bottom to top. Each module is a `.c/.h` pair under `src/<dir>/`. Arrows = "may
call". No upward calls. No sibling calls except through a defined interface.

| Layer | Module | Responsibility | May call |
|---|---|---|---|
| 0 | `board/board` | Clock setup (100 MHz HSE PLL), pin/alt-function config, EN pin, brown-out. One `board_init()`. | StdPeriphDriver |
| 0 | `board/time` | `time_now_ms()`, `time_now_us()` (free-running), busy `delay_us()`. Backs failsafe timing and jitter measurement. | core SysTick / a counter |
| 1 | `i2c/i2c_master` | CH57x I²C master. **Blocking** API with timeouts + bus-stuck recovery (present today in `Main.c`), plus an **IRQ-driven** `i2c_write_async()` for later. Never hangs. | board, StdPeriphDriver |
| 2 | `drv2605/drv2605_regs.h` | DRV2605**L** register addresses + bit masks. **Header only.** Every entry cites the datasheet section. | — |
| 2 | `drv2605/drv2605` | Generic register driver: `drv2605_init()`, `drv2605_write_reg/read_reg`, `drv2605_probe()` (DEVICE_ID), `drv2605_read_status()` (OVER_TEMP/OC/VBAT). | i2c |
| 2 | `drv2605/drv2605_lra` | Open-loop LRA specifics: `drv2605_lra_configure(olp, od_clamp)`, `drv2605_set_amplitude(int8)` (writes reg 0x02, **skips the write when unchanged**), calibration/diagnostic routines, the resonance sweep + `LRA_PERIOD` readback (bench tools). | drv2605 |
| 3 | `haptic/haptic_fifo.h` | Single-producer / single-consumer ring of `int8` samples. **Header-only**, lock-free (see §"Concurrency"). Power-of-two capacity. | — |
| 3 | `haptic/haptic_engine` | Owns the FIFO and the current output amplitude. `haptic_tick()` (called from the main loop once per SysTick): pop 1 sample (or step an envelope), apply `amp_max` clamp, call `drv2605_set_amplitude()`, update stats. Owns `failsafe` (ramp to 0 when `time_now_ms() - last_frame_ms > failsafe_ms`). Owns mode (idle / samples / envelope / local-test). | haptic_fifo, drv2605_lra, board/time |
| 3 | `haptic/haptic_patterns` | Local, PC-independent test patterns (boot self-test click, the sim-racing demo currently in `Main.c`). Only used when `mode == local-test` or at boot. | haptic_engine, drv2605_lra |
| 4 | `link/proto.h` | **The frozen wire contract.** Frame layout, `TYPE_*` codes, config/stats structs, `PROTO_VERSION`. **Kept byte-identical with the PC copy under `tools/proto/`.** See [PROTOCOL.md](PROTOCOL.md). | — |
| 4 | `link/link` | Framing (CRC8 verify, length check), per-direction sequence tracking + gap accounting, dispatch: DATA→`haptic_fifo` push, ENVELOPE→engine, CTRL_*→`link_control`, RESYNC→`haptic_abort`. Builds outbound frames (PONG/STATUS/LOG/FAULT) via an injected `link_send_fn`. Pure logic, no transport knowledge, no `board/time` (uses `haptic_now_ms()`). | haptic_engine, haptic_fifo, util/crc |
| 4 | `link/link_control` | Handlers for `CTRL_SET_CONFIG` (validate every field → `haptic_apply_config` or `FAULT_BAD_CONFIG`/`FAULT_VERSION`), `CTRL_SET_MODE`, `CTRL_PING`, `STATUS_REQ`. Config-driven DRV2605 reprogramming happens inside the engine's staged-config apply, so `link_control` needs only `haptic_engine`. | haptic_engine, link (tx builders) |
| 5 | `transport/transport.h` | `transport_t` vtable: `init()` (wire link↔pipe), `poll()` (main-loop pump), `host_active()`. | — |
| 5 | `transport/transport_usb` | Binds `usb_vendor` (EP2) to `link`: `link_init(usb_send_adapter)` + `usb_vendor_set_rx(link_rx)`; `poll()` = `usb_vendor_poll()`. `TRANSPORT_USB_TX_TRACE` decodes outbound frames to the CDC log. | usb, link |
| 5 | `transport/transport_rf` | *(Phase 4)* Binds the 2.4 GHz RX/TX to `link_rx()` / `link` outbound. | rf, link |
| 5 | `usb/usb_device` | The composite device: descriptors (IAD: CDC iface 0–1 + vendor iface 2), enumeration SM, `USB_IRQHandler`, all EP dispatch. Evolved from the bench-proven CDC logger. MS OS 2.0 descriptors → Phase 3.6. | StdPeriphDriver usbdev |
| 5 | `usb/usb_cdc.h` | CDC-ACM channel API — `usb_cdc_write/printf/connected` (EP1 IN log ring). Implemented in `usb_device.c`. | — |
| 5 | `usb/usb_vendor.h` | Vendor bulk API — `usb_vendor_set_rx` / `_poll` / `_send` / `_host_active` / `_stats` (EP2). ISR stages packets, main loop parses. Implemented in `usb_device.c`. | link/proto.h (frame size cap only) |
| 5 | `rf/rf_link` | *(Phase 4)* WCH 2.4 GHz proprietary mode wrapper. Reference: `EVT/EXAM/RF/RF_Basic` and `EVT/EXAM/RF/RF_UartDongle`. | StdPeriphDriver RF |
| 6 | `log/log` | `log_printf(level, fmt, …)` → internal ring buffer, drained by the main loop into the **current sink** (`log_set_sink()`): CDC write, `LOG` frame via `link`, UART TX, or RF back-channel. Levels `ERR/WARN/INFO/DBG`. **Never called from an ISR.** | (sink is injected) |
| 6 | `util/ringbuf.h`, `util/crc` | Generic byte ring; CRC8 (poly 0x07) used by `link` and RF. | — |
| 7 | `main` | `board_init()` → peripherals → `drv2605_init()` + probe + `drv2605_lra_configure()` → boot self-test → transport init → super-loop. | everything |

### Cross-cutting

- **`src/config.h`** — every compile-time tunable in one place: default
  `OL_LRA_PERIOD`, `OD_CLAMP`, `sample_rate_hz`, `failsafe_ms`, FIFO size, log
  ring size, pin numbers, feature flags (`FEATURE_RF`, `FEATURE_DATAFLASH_CFG`).
  Runtime-overridable values (from `CTRL_SET_CONFIG`) live in a `struct config`
  owned by `haptic_engine`, seeded from `config.h`.
- **`stats`** — a plain struct of counters in `haptic_engine`, incremented from
  anywhere (including ISRs — they are `uint16/uint32`, single-writer per field
  where it matters, torn reads are acceptable for a diagnostic). Serialized by
  `link_control` on `STATUS_REQ`. This — not text logging — is how you observe
  the box during streaming.

---

## 3. Concurrency and the hot path

Execution contexts, highest priority first:

| Context | Rate | What it may do | What it must NOT do |
|---|---|---|---|
| **SysTick ISR** | `sample_rate_hz` (default 1000) | Increment `g_tick` (a `volatile uint32_t`). Nothing else. | Anything else. |
| **USB ISR** | per USB transaction | Service EP0/CDC as today. For the data bulk OUT: copy the transfer into a staging buffer and either (a) push complete frames' samples straight into `haptic_fifo` (O(1)), or (b) set a "rx pending" flag for the main loop. Increment `stats.frames_rx`. | Block, log, `malloc`, call `drv2605_*`, do work proportional to FIFO depth. |
| **RF ISR** *(Phase 4)* | per RF packet | Same contract as the USB data path. | Same. |
| **Main super-loop** | as fast as it runs; paced by `g_tick` | Everything else: `link` parsing, `haptic_tick()` (pops FIFO, writes DRV2605 over blocking I²C), failsafe check, drain the log ring to the sink, ~10 Hz refresh of DRV2605 STATUS/VBAT into `stats`. | — |

### The FIFO contract (`haptic_fifo`)

- **Single producer**: the USB (or RF) ISR, pushing samples.
- **Single consumer**: the main loop, in `haptic_tick()`.
- RV32 aligned 32-bit `head`/`tail` loads/stores are atomic. Producer writes the
  sample data, then a `__sync_synchronize()` / compiler barrier, then publishes
  the new `head`. Consumer reads `tail`, reads data, barrier, publishes `tail`.
  No disable-interrupts needed because there is exactly one of each.
- **Overrun** (producer catches consumer): drop the *newest* sample, increment
  `stats.fifo_overrun`. Never overwrite unread data.
- **Underrun** (consumer catches producer): output the last sample decaying
  linearly toward 0 over `config.underrun_decay_ms`; increment
  `stats.fifo_underrun`. Never read stale garbage.
- Capacity: enough for `~40 ms` at `sample_rate` (e.g. 64 samples at 1 kHz →
  round up to 128). Bigger = more loss tolerance, more latency. `config.h`.

### Why the sample write is in the main loop, not the ISR

One DRV2605 RTP write over 400 kHz I²C is **~90–110 µs** (see §"Timing budget").
Doing that inside the SysTick ISR would stall USB/RF servicing by ~100 µs per
tick. Instead the ISR only bumps `g_tick`; the main loop does the write. Added
latency is at most one main-loop iteration (tens of µs). If the main loop ever
falls behind, `g_tick` advances by more than 1 between iterations — detect that
and record it in `stats.tick_jitter_us_max` / a `tick_backlog` counter.

Phase 6 may move the write to `i2c_write_async()` (IRQ-driven) if headroom is
ever needed; the module boundary already allows it.

---

## 4. Timing budget

Numbers for the CH570D at 100 MHz, DRV2605L I²C at 400 kHz (its maximum).

| Item | Cost | Notes |
|---|---|---|
| One RTP write (`START`+addr+reg+data+`STOP`) | ~90–110 µs on the wire | ≈ 29 bit-times @ 400 kHz + software + clock-stretch margin |
| Max sustainable RTP rate over I²C | ~5–8 kHz | but see below |
| **Chosen `sample_rate_hz`** | **1000 (up to 2000)** | 1 kHz → I²C bus ~10 % busy, ~10 % main-loop CPU. 2 kHz → ~20 %. Both leave large margin. |
| `link` parse of one frame | tens of µs | CRC8 over ≤ 60 bytes + memcpy |
| USB FS frame period | 1 ms | data plane sends ≥ 1 packet/frame |
| Actuator mechanical rise time | **~10–20 ms** | This dominates perceived latency. It is why 8 kHz sample rates are pointless for a single LRA — the mechanism cannot reproduce envelope detail faster than ~50–100 Hz. |
| End-to-end event→vibration | target < ~5 ms of *electronics* latency (PC compute + transport + small FIFO), then + actuator rise | Keep the FIFO shallow (trade vs jitter tolerance). |

**Design consequence**: everything above I²C is comfortable at 1–2 kHz. The only
way to exceed ~4 kHz cleanly would be DRV2605 **PWM-input mode** (drive the
`IN/TRIG` pin from a timer + DMA, I²C for config only). `IN/TRIG` is **not routed
on the current board**, so this path is out of scope until a hardware revision.
If it is ever added: it slots in as an alternate "sink" behind
`drv2605_set_amplitude()` — nothing above `drv2605_lra` changes.

---

## 5. Directory layout (target)

```
CH570D/
├── src/
│   ├── main.c
│   ├── config.h                 # all compile-time tunables + feature flags
│   ├── board/
│   │   ├── board.c / board.h     # clock, pins, EN, brown-out
│   │   └── time.c / time.h       # ms/us clock, delay_us
│   ├── i2c/
│   │   └── i2c_master.c / .h     # blocking (timeouts + recovery) + async
│   ├── drv2605/
│   │   ├── drv2605_regs.h        # DRV2605L registers + bits (cite datasheet)
│   │   ├── drv2605.c / .h        # init, probe, reg r/w, status/vbat
│   │   └── drv2605_lra.c / .h    # open-loop config, set_amplitude, sweep, cal
│   ├── haptic/
│   │   ├── haptic_fifo.h         # SPSC int8 ring (header-only)
│   │   ├── haptic_engine.c / .h  # tick, envelope, failsafe, mode, stats, config
│   │   └── haptic_patterns.c / .h
│   ├── link/
│   │   ├── proto.h               # FROZEN wire contract  (mirror of tools/proto/)
│   │   ├── link.c / .h           # framing, CRC, seq, dispatch, outbound frames
│   │   └── link_control.c / .h   # CTRL_* handlers
│   ├── transport/
│   │   ├── transport.h
│   │   ├── transport_usb.c / .h
│   │   └── transport_rf.c / .h        # Phase 4
│   ├── usb/
│   │   ├── usb_device.c / .h          # composite descriptors + MS OS 2.0
│   │   ├── usb_cdc.c / .h             # from usb_log.c
│   │   └── usb_data_ep.c / .h
│   ├── rf/
│   │   └── rf_link.c / .h             # Phase 4
│   ├── log/
│   │   └── log.c / .h
│   └── util/
│       ├── ringbuf.h
│       └── crc.c / .h
├── tools/
│   ├── proto/proto.h            # byte-identical copy of src/link/proto.h
│   └── pc_sender/               # reference host app (libusb / pyusb)
├── targets/
│   ├── box/                     # main firmware build config
│   └── dongle/                  # USB(PC) <-> RF bridge firmware  (Phase 4)
├── docs/                        # this folder
├── Startup/ Ld/ RVMSIS/ StdPeriphDriver/   # WCH SDK — DO NOT EDIT
├── EVT/                         # WCH examples + converted datasheets — reference only
└── obj/                         # MRS build output
```

> **Build-system note**: MounRiver Studio regenerates `obj/**/subdir.mk` from the
> project's source folders on each build. Adding files under `src/` is picked up
> automatically *if* `src` is a recursive source folder in `.cproject`. See
> [BUILD.md](BUILD.md) for adding a new directory and for the manual-`make` path.

---

## 6. Key design decisions (and why)

| Decision | Rationale |
|---|---|
| Box is a **renderer**, PC is the **mixer** | User requirement. Keeps firmware tiny and lets effect design iterate on the PC without reflashing. Box only needs FIFO + failsafe + a boot pattern. |
| **Open-loop** LRA drive, fixed `OL_LRA_PERIOD` | The Apple Taptic Engine's back-EMF is too weak/noisy for the DRV2605 closed loop (auto-cal fails, the resonance tracker locks onto a 1/3 subharmonic). Evidence in [HARDWARE.md](HARDWARE.md). Open-loop at the measured resonance is strong and reliable. |
| **`sample_rate_hz` = 1000–2000**, not 8000 | I²C RTP writes cap sustainable rate ~5–8 kHz; more importantly the actuator responds at ~10–20 ms, so envelope detail above ~100 Hz is inaudible. 1–2 kHz gives margin and low latency. |
| **One link layer, transport-agnostic** | USB now, RF next revision. The parser must not care which pipe delivered the bytes. Transports implement a common vtable. |
| **Packet-oriented protocol** (not a COBS byte stream) | Both real transports (USB bulk, RF) deliver discrete datagrams. A 4-byte header + CRC8 per packet is simpler and matches both. The CDC log path stays raw text. |
| **CRC8 always**, even on USB | USB bulk already has link CRC, but carrying our own keeps the format identical across transports and catches framing bugs. Cheap. |
| **SysTick for the tick**, not the TMR | The CH572/CH570 has a **single** general-purpose `TMR` (shared with PWM/CAP/DMA). Reserve it. The RISC-V core SysTick (`0xE000F000`) is free — **confirmed** in Phase 1: `mDelayuS`/`mDelaymS` (in `libISP572.a`) are pure calibrated NOP loops (25 iterations/µs), they do **not** touch SysTick. `src/board/time.c` owns SysTick via `SysTick_Config()` + `SysTick_Handler` (`SysTick_IRQn` = 12). |
| **SPSC lock-free FIFO** | Exactly one producer (rx ISR) and one consumer (main loop) → no critical sections, no priority inversion, deterministic. |
| **Composite USB device** (CDC + vendor bulk) | Keeps `screen`-able logs for dev convenience while the vendor bulk pipe carries data without CDC's line-discipline overhead. |
| **Logging is a pluggable sink** | The same `LOG` output must work over USB today and RF/UART later with zero application changes. |
| **`stats` struct over `GET_STATUS`**, not text logs, during streaming | Text logging in a 1 kHz loop is the wrong tool. Counters + a PC dashboard scale; text stays for rare discrete events. |

---

## 7. Known vendor issues

- `StdPeriphDriver/CH57x_i2c.c :: I2C_Init()` writes `sysClock/1000000` (= 100)
  into `R16_I2C_CTRL2` — the `FREQ` field is only 6 bits, so this also sets a
  reserved bit and truncates `FREQ` to `100 & 0x3F = 36`. The official
  `EVT/EXAM/I2C` example runs the same way at 100 MHz and I²C works, so the
  bring-up firmware uses it as-is. If I²C timing ever misbehaves, this is the
  first suspect — fix in project code by writing `R16_I2C_CTRL2` correctly after
  `I2C_Init()`, do not edit the vendor file.
- `EVT/DRV2605_interface.md` is the **DRV2605** (non-L) datasheet. Do not use it
  for `CONTROL5` (0x1F), `OL_LRA_PERIOD` (0x20), `ZC_DET_TIME`, or field widths.
  See [HARDWARE.md](HARDWARE.md).
