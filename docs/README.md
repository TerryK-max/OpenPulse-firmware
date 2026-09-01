# OpenPulse firmware — Documentation

> **If you are an AI agent or a new contributor: read this file completely before
> touching any code.** It tells you what the project is, what state it is in,
> what you may and may not change, and which other document to open next.

---

## 1. What this project is

A small USB / 2.4 GHz haptic-feedback box for **sim racing**.

- **MCU**: WCH **CH570D** (CH572/CH570 family, 32-bit RISC-V, USB Full-Speed
  device, 2.4 GHz proprietary radio). Runs at 100 MHz from the HSE PLL.
- **Haptic driver**: **DRV2605L** (Texas Instruments) over I²C.
  ⚠️ It is the **-L variant** (low-voltage). See [HARDWARE.md](HARDWARE.md).
- **Actuator**: a salvaged **Apple iPhone XS Max "Taptic Engine"** (an LRA),
  driven **open-loop** at its ~158.7 Hz resonance. See [HARDWARE.md](HARDWARE.md)
  for why closed-loop / auto-calibration does not work with this actuator.
- **Data flow**: a PC computes and **mixes all haptic effects + sound textures
  into a single amplitude waveform** and streams it to the box in the smallest
  possible packets. The box is a thin **renderer**: receive → buffer → drive the
  DRV2605. It performs no effect synthesis of its own (only a boot self-test and
  a safety ramp-down).
- **Transports**: **USB now**, **2.4 GHz (paired dongle) at the next hardware
  revision**. Both feed the *same* protocol parser.
- **Target update rate**: **1–2 kHz** amplitude samples (this is comfortably
  within what blocking I²C to the DRV2605 can sustain; see
  [ARCHITECTURE.md](ARCHITECTURE.md) §"Timing budget").

---

## 2. Current state (2026-09-02)

**Phases 0–3 are implemented** (Phase 3 awaits its on-bench check —
[ROADMAP.md](ROADMAP.md) 3.5). Phase 1 is bench-confirmed. Phase 2 is the
transport-agnostic **link layer** (`src/link/`, `src/util/crc.c`), covered by a
host harness (`make test`, 83 checks). Phase 3 made USB a **composite device**:

- `src/usb/usb_device.c` — CDC-ACM (interfaces 0–1, the `/dev/cu.usbmodem*` log
  port) **+** a vendor bulk interface (2, EP2) carrying the framed protocol.
  `usb_cdc.h` / `usb_vendor.h` are the two channel APIs.
- `src/transport/transport_usb.c` — binds EP2 ↔ `link` (`link_rx` on the main
  loop, `link_tx_*` → EP2 IN).
- `tools/pc_sender/` — Python reference sender (pyusb) + `proto.py`.

The renderer runs a **local self-test** when no PC is attached (the generator
frames `DATA_SAMPLES` into `link_rx()`); it steps aside the instant a PC starts
sending on EP2. The bench bring-up harness is still one
`#define DRV2605_BENCH_TOOLS 1` away.

| Path | What it is |
|---|---|
| `Makefile` | Standalone build (`make`, `make check`, `make clean`). Mirrors the MRS flags, writes to `build/`. MRS still works via `CH570D.wvproj`. See [BUILD.md](BUILD.md). |
| `src/main.c` | Entry point. `#if DRV2605_BENCH_TOOLS` = bench flow (Phase 0); `#else` = the **Phase 1 renderer** (default): probe → open-loop RTP armed → `haptic_engine` → SysTick clock → `MODE_SAMPLES` fed by the generator, `[R]` stats every 2 s. |
| `src/config.h` | All compile-time tunables. `[MEASURED]` = don't change without a bench test; `[RUNTIME]` = seeds for `haptic_config_t`. |
| `src/board/` | `board_init()` (clock, debug-pin); `time.{c,h}` — delay aliases + the **SysTick sample clock** (`time_tick_start` / `time_ticks` / `time_now_ms`, `SysTick_Handler`). |
| `src/i2c/i2c_master.{c,h}` | Blocking I²C master: `i2c_write` / `i2c_read_reg` / `i2c_master_init` / `_recover`, timeouts + NACK detect. |
| `src/drv2605/` | `drv2605_regs.h`; `drv2605.{c,h}` (reg r/w, `en_assert`, `reset`); `drv2605_lra.{c,h}` (open-loop config, `drv2605_set_amplitude` with write-skip cache); `drv2605_bench.{c,h}` (verbose harness, `#if DRV2605_BENCH_TOOLS`). |
| `src/haptic/` | `haptic_fifo.h` (SPSC int8 ring); `haptic_engine.{c,h}` (tick / modes / config / stats / failsafe / `haptic_abort` / `haptic_now_ms`); `haptic_patterns.{c,h}` (`haptic_pattern_simracing` generator, `haptic_pulse`, `haptic_demo_simracing`). |
| `src/link/` | `proto.h` (FROZEN wire contract, mirror of `tools/proto/proto.h`); `link.{c,h}` (framing / CRC / SEQ / gap / dispatch + `link_tx_*` builders, transport-agnostic); `link_control.{c,h}` (`CTRL_*` handlers). See [PROTOCOL.md](PROTOCOL.md). |
| `src/util/crc.{c,h}` | CRC-8/SMBUS (poly 0x07). `proto_crc8()` alias. |
| `src/usb/` | `usb_device.{c,h}` — composite CDC + vendor-bulk device (descriptors, enumeration, `USB_IRQHandler`, all EP dispatch); `usb_cdc.h` (log channel); `usb_vendor.h` (protocol channel, EP2). See [HARDWARE.md](HARDWARE.md) §5. |
| `src/transport/` | `transport.h` (`transport_t` vtable); `transport_usb.{c,h}` (binds EP2 ↔ `link`). |
| `src/log/log.{c,h}` | Logging facade → `usb_cdc` (levels + macros; runtime sink switch = Phase 3.3, deferred). |
| `tools/proto/`, `tools/test/` | Byte-identical `proto.h` copy for the PC; host unit harness for `src/link/` (`make test`). |
| `tools/pc_sender/` | Python reference sender (`openpulse_send.py` + `proto.py`) — streams a waveform over the vendor pipe, prints the STATUS dashboard. |
| `Startup/`, `Ld/`, `RVMSIS/`, `StdPeriphDriver/` | WCH vendor SDK — **not in git** (their copyright). Fetch per [SETUP.md](SETUP.md). **Do not edit.** |
| `EVT/` | WCH evaluation package + converted datasheets. **Not in git.** Fetch per [SETUP.md](SETUP.md). Reference only. |
| `docs/vendor/` | Place third-party datasheets here for offline reference — `.gitignore`d ([vendor/README.md](vendor/README.md)). |
| `build/`, `obj/` | Build output (Makefile / MRS). Regenerated; not source; not in git. |

The bring-up **works on the bench**: the actuator is confirmed functional,
resonance found, drive level tuned, thermals acceptable. Those tuned values are
in `src/config.h` and transcribed in [HARDWARE.md](HARDWARE.md) — **treat them as
measured constants, not guesses.**

---

## 3. Operating rules for agents

1. **You cannot run the hardware.** There is no way for you to flash the board or
   feel the motor. Your correctness bar is: **it compiles clean** (see
   [BUILD.md](BUILD.md)) and it follows this documentation. Any change that
   affects DRV2605 register values, drive timing, the resonance frequency, the
   sample rate, or ISR timing **must be proposed to the user for on-hardware
   testing** and the user's result recorded back into [HARDWARE.md](HARDWARE.md).

2. **The DRV2605L register map is NOT the one in `EVT/DRV2605_interface.md`.**
   That converted doc is for the plain DRV2605. The board has a **DRV2605L**,
   which adds registers (`CONTROL5` 0x1F, `OL_LRA_PERIOD` 0x20) and widens
   fields. Before writing or trusting any DRV2605 register, cross-check it
   against the TI DRV2605L datasheet (SLOS854C) — see
   [HARDWARE.md](HARDWARE.md) §2.1 for the link and
   [vendor/README.md](vendor/README.md). Never guess a register address, bit
   position, or reset value: cite your source.

3. **The wire protocol is frozen and versioned.** Any change to frame layout,
   type codes, or struct fields in [PROTOCOL.md](PROTOCOL.md) requires bumping
   `PROTO_VERSION`, updating the PC-side code under `tools/`, and updating that
   document — all in the same change.

4. **The hot path never blocks and never logs.** The sample-rate tick and the
   USB/RF receive ISRs must not call the logger, must not spin on I²C, must not
   do work proportional to anything unbounded. They touch a lock-free FIFO and
   integer counters only. See [ARCHITECTURE.md](ARCHITECTURE.md) §"Concurrency".

5. **Fail safe, always.** If telemetry stops arriving (host crash, unplug, RF
   loss) the box must ramp the actuator to zero within `failsafe_ms`. A silent
   actuator is always an acceptable failure mode; a stuck-on actuator is not
   (it overheats — see [HARDWARE.md](HARDWARE.md) §"Thermal limits").

6. **Match the surrounding code style.** WCH SDK style is `PascalCase` public
   functions with `/*** @fn ... */` banners; project code is `lower_snake_case`
   with terse comments. Keep new project modules in the project style.

7. **Do not edit `StdPeriphDriver/`, `Startup/`, `RVMSIS/`, `Ld/`, `EVT/`.**
   If a vendor-driver bug blocks you, wrap around it in project code and note it
   in [ARCHITECTURE.md](ARCHITECTURE.md) §"Known vendor issues".

---

## 4. Which document to read next

| You want to… | Read |
|---|---|
| Get the vendor SDK / EVT package in place so it builds at all | [SETUP.md](SETUP.md) |
| Understand the module boundaries, data flow, ISR model, directory layout | [ARCHITECTURE.md](ARCHITECTURE.md) |
| Implement or change the PC↔box packet format | [PROTOCOL.md](PROTOCOL.md) |
| Know the pin map, the DRV2605L quirks, the actuator tuning, thermal limits | [HARDWARE.md](HARDWARE.md) |
| Know what to build next and in what order (tickets with acceptance criteria) | [ROADMAP.md](ROADMAP.md) |
| Compile, add a source file, flash, read logs | [BUILD.md](BUILD.md) |
| Know what's third-party and under which licence | [../THIRD_PARTY.md](../THIRD_PARTY.md), [../LICENSE](../LICENSE) |

---

## 5. Glossary

| Term | Meaning |
|---|---|
| **LRA** | Linear Resonant Actuator. A voice-coil + mass + spring that vibrates efficiently only near its mechanical **resonance** (~158.7 Hz here). Behaves like a narrow band-pass; you can modulate its *amplitude*, not reproduce arbitrary audio. |
| **Open-loop drive** | DRV2605 mode where the chip commutates the LRA at a fixed programmed period (`OL_LRA_PERIOD`, reg 0x20) instead of tracking back-EMF. Used here because the Apple actuator's back-EMF is too weak/noisy for the closed loop. |
| **RTP** | Real-Time Playback. DRV2605 mode 5: the value written to reg `0x02` is driven to the actuator continuously. This is our amplitude input. |
| **Sample** | One signed 8-bit amplitude value (`-127..+127`; `0` = no drive). The unit of the data stream. |
| **Tick** | One iteration of the fixed-rate playout clock (`sample_rate_hz`, default 1000). One sample is consumed per tick. |
| **Frame / packet** | One protocol unit on the wire (USB bulk transfer or RF packet). Carries a batch of samples or a control message. See [PROTOCOL.md](PROTOCOL.md). |
| **Sink** (logging) | A destination for log bytes: CDC serial, a `LOG` protocol frame, a debug UART pin, or (later) the RF back-channel. Selected at runtime by `log_set_sink()`. |
| **Failsafe** | Automatic ramp-to-zero when no valid data frame has arrived for `failsafe_ms`. |
| **MRS** | MounRiver Studio — the WCH Eclipse-based IDE / build system for this project. |
