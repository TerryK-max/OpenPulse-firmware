# Hardware

Read [README.md](README.md) first. This document is the source of truth for the
board, the DRV2605L, and the actuator. Values marked **[measured]** were found on
the bench during bring-up (2026-08-30/31) and must not be changed without a new
on-hardware test.

---

## 1. MCU — WCH CH570D (CH572/CH570 family)

- 32-bit RISC-V (`rv32imc` + WCH extensions), single core.
- Runs at **100 MHz** from the HSE crystal + PLL (`SetSysClock(CLK_SOURCE_HSE_PLL_100MHz)`).
  Required for reliable USB Full-Speed. HSE load caps: `HSECFG_Capacitance(HSECap_18p)`.
- USB **Full-Speed device** controller (12 Mbit/s).
- **2.4 GHz** proprietary radio (also BLE-capable; we use the proprietary mode).
- Peripherals of interest: I²C ×1, UART ×1, **one** general timer `TMR`
  (shared with PWM/capture/DMA — reserve it), core **SysTick**, ADC (via
  PWM+comparator on this family), IWDG, on-chip DataFlash.
- Converted CH572/CH570 datasheet: `EVT/DOCS/` (per-peripheral Markdown; see
  `EVT/DOCS/README.md` for its own agent rules).

### 1.1 Pin map (as wired on the current board)

| Pin | Function here | Notes |
|---|---|---|
| **PA0** | USB **D−** (UDM) | do not reuse |
| **PA1** | USB **D+** (UDP) | do not reuse |
| **PA8** | I²C **SCL** to DRV2605L | default I²C mapping (`RB_I2C_PIN = 00` in `R16_PIN_ALTERNATE_H`) |
| **PA9** | I²C **SDA** to DRV2605L | " |
| **PA11** | DRV2605L **EN** (GPIO output, driven high by firmware) | `src/Main.c` `DRV2605_EN_PIN GPIO_Pin_11` — the code comment still says "PA4", the **pin number is correct, the comment is stale** |
| PA2, PA3, PA5, PA6 | free | PA3/PA2 and PA5/PA6 are alternate I²C mappings if PA8/PA9 are ever needed for something else |
| **PA7** | **free — reserved for the debug UART TX** (see [ARCHITECTURE.md](ARCHITECTURE.md) §"logging", [ROADMAP.md](ROADMAP.md) Phase 3) | UART TXD can remap to PA0/1/2/3/7/8/10/11; PA7 is the clean choice |
| PA10 | free — or `BENCH_GPIO_TRACE` **render-tick** marker (toggles per `drv2605_set_amplitude()`) | second UART-TX-capable option |
| PA4 | free — or `BENCH_GPIO_TRACE` **frame-rx** marker (toggles per `DATA_SAMPLES`) | scope points for [BENCH.md](BENCH.md) §2 |

- The **2-wire debug interface is on PA8/PA9** and is **disabled in firmware**
  (`R16_PIN_ALTERNATE &= ~RB_PIN_DEBUG_EN` in `main()`) so those pins can be I²C.
  Consequence: **you cannot use SWD-style debug / SDI-Print while I²C is active.**
  On-target debugging is via logs (see [ARCHITECTURE.md](ARCHITECTURE.md) §"logging").
- **`IN/TRIG` of the DRV2605L is NOT routed to the MCU** (grounded or floating on
  this board). DRV2605 PWM-input / analog-input / external-trigger modes are
  therefore **unavailable**. Everything goes through I²C. A future board revision
  could route `IN/TRIG` → PA7/PWM1 to unlock high-rate PWM streaming; see
  [ARCHITECTURE.md](ARCHITECTURE.md) §"Timing budget".

### 1.2 I²C peripheral notes

- DRV2605L is at **7-bit address `0x5A`** → `0xB4` write / `0xB5` read.
- Bus speed: **400 kHz** (DRV2605L maximum).
- **[MEASURED] one register write (RTP, reg 0x02) ≈ 126 µs** end-to-end
  (2000 writes in 253 ms, Phase 1 boot measurement). ≈ 75 µs on the wire
  (START + addr + reg + data + ACKs + STOP) + ~50 µs of `I2C_Init`-driver
  polling overhead per call. → **max ~7.9 kHz** sustained RTP writes. At the
  Phase 1 `HAPTIC_SAMPLE_RATE_HZ = 1000` that is ~12.6 % bus load worst case
  (every sample different); the unchanged-value write-skip cache makes the
  typical load far lower. 2 kHz (~25 %) is also comfortable. Headroom vs the
  1 kHz default: ~7.9×.
- The bring-up firmware uses the WCH `I2C_Init()` as-is. It mis-programs the
  `FREQ` field at 100 MHz (see [ARCHITECTURE.md](ARCHITECTURE.md) §"Known vendor
  issues") but I²C works — the official `EVT/EXAM/I2C` example does the same.
- Blocking master with time-outs + bus-stuck recovery is implemented in
  `src/Main.c` (`i2c_wait_*`, `drv2605_write`, `drv2605_read_reg`,
  `drv2605_i2c_bus_recover`). Phase 0 extracts this to `src/i2c/i2c_master`.

---

## 2. Haptic driver — TI **DRV2605L** (not DRV2605!)

- Confirmed on the bench: `STATUS` register (0x00) reads DEVICE_ID = **7** =
  **DRV2605L** (the low-voltage variant). It *does* contain the licensed ROM
  effect library.
- VDD on this board = **5 V**. DRV2605L absolute range is **2.0–5.2 V**, so 5 V
  is in spec (this is a difference from the plain DRV2605, which is 2.5–5.5 V).
- Package REG pin needs 1 µF, VDD needs 0.1 µF (assumed populated).

### 2.1 Reference material — READ BEFORE TOUCHING ANY DRV2605 REGISTER

- ✅ **TI DRV2605L datasheet, SLOS854C** — <https://www.ti.com/lit/ds/symlink/drv2605l.pdf>.
  **This is the authoritative register map for this chip.** Section numbers
  (`§8.6.x`) cited below and in the code refer to it. It is TI copyright and is
  **not** in this repo — download it and drop it in `docs/vendor/` (gitignored;
  see [vendor/README.md](vendor/README.md)). The register facts this project
  relies on are transcribed, with citations, into §2.2 below and
  `src/drv2605/drv2605_regs.h`.
- ⚠️ `EVT/DRV2605_interface.md` — converted datasheet for the **plain DRV2605**.
  Good for the *common* registers and the effect list, **wrong** for the
  L-specific ones. Do **not** use it for `CONTROL5` (0x1F), `OL_LRA_PERIOD`
  (0x20), `ZC_DET_TIME`, or bit-field widths.

### 2.2 Registers this project actually uses

| Addr | Name | Used for | Source (SLOS854C) |
|---|---|---|---|
| 0x00 | `STATUS` | DEVICE_ID (7-5), DIAG_RESULT (3), FB_STS (2), OVER_TEMP (1), OC_DETECT (0). Cleared on read. | §8.6.1 |
| 0x01 | `MODE` | `0x00` internal-trigger, `0x05` **RTP** (our drive mode), `0x07` auto-cal, bit7 DEV_RESET, bit6 STANDBY | §8.6.2 |
| 0x02 | `RTP_INPUT` | **the amplitude input.** Signed by default. `drv2605_set_amplitude()` writes here. | §8.6.3 |
| 0x03 | `LIBRARY_SEL` | ROM library select (6 = LRA); only used by the bench effect helpers | §8.6.4 |
| 0x0C | `GO` | trigger bit, self-clearing; used by cal + the ROM-effect bench helpers | §8.6.13 |
| 0x16 | `RATED_VOLTAGE` | closed-loop reference — **ignored in open-loop** (§8.5.2.1). Left at a plausible value for the (unused) cal path. | §8.6.16 |
| 0x17 | `OD_CLAMP` | **open-loop full-scale / peak-voltage reference** (§8.5.2.2: *"always represents the maximum peak voltage allowed, regardless of mode"*). Default `0x8C` (~3.0 V). ERM open-loop: `V ≈ 21.59 mV × OD_CLAMP` (eq. 6). LRA open-loop RMS scales similarly (eq. 7, same order). | §8.6.17 |
| 0x18/0x19 | `A_CAL_COMP` / `A_CAL_BEMF` | auto-cal outputs. **Reset defaults on the L are `0x0C` / `0x6C`** — remember this (see §3). | §8.6.18/19 |
| 0x1A | `FEEDBACK_CONTROL` | N_ERM_LRA (7), FB_BRAKE_FACTOR (6-4), LOOP_GAIN (3-2), BEMF_GAIN (1-0). We set N_ERM_LRA=1. | §8.6.20 |
| 0x1B | `CONTROL1` | DRIVE_TIME (4-0), 5-bit, max 31. LRA: `drive_ms = DRIVE_TIME×0.1 + 0.5`. Only matters for the (unused) closed loop; kept as a sane guess. | §8.6.21 |
| 0x1C | `CONTROL2` | SAMPLE_TIME, BLANKING_TIME[1:0], IDISS_TIME[1:0]. Left at reset `0xF5`. | §8.6.22 |
| 0x1D | `CONTROL3` | **bit 0 `LRA_OPEN_LOOP` = 1** (this is what makes us open-loop). bit 5 ERM_OPEN_LOOP (n/a). | §8.6.23 |
| 0x1E | `CONTROL4` | ZC_DET_TIME (7-6), AUTO_CAL_TIME (5-4). Reset `0x20`. | §8.6.24 |
| 0x1F | `CONTROL5` **(L only)** | AUTO_OL_CNT (7-6), LRA_AUTO_OPEN_LOOP (5), PLAYBACK_INTERVAL (4), BLANKING_TIME[3:2], IDISS_TIME[3:2]. Reset `0x80`. | §8.6.25 |
| 0x20 | `OL_LRA_PERIOD` **(L only)** | **open-loop commutation period.** `period_µs = OL_LRA_PERIOD[6:0] × 98.46`. Reset `0x33` (~199 Hz). **[measured] value = 64 → ~158.7 Hz** (see §3). | §8.6.26 |
| 0x21 | `VBAT` | VDD monitor during drive: `VDD ≈ VBAT × 5.6 / 255`. Read into `stats.drv_vbat`. | §8.6.27 |
| 0x22 | `LRA_PERIOD` | measured resonance period during closed-loop drive: `µs = val × 98.46`. **Unreliable on this actuator** (see §3) — bench diagnostic only. | §8.6.28 |

- DRV2605L **minimum supported LRA resonance = 125 Hz** (datasheet changelog:
  *"Changed minimum supported resonant frequency from 50 Hz to 125 Hz"*). Our
  ~158.7 Hz is comfortably inside the range.

---

## 3. Actuator — Apple iPhone XS Max "Taptic Engine" (LRA)

A salvaged Apple wide-body LRA. **[measured] coil DCR ≈ 9.65 Ω.**

### 3.1 Why we drive it open-loop (do not "fix" this)

Closed-loop auto-resonance / auto-calibration **does not work** with this
actuator. Evidence gathered on the bench, repeated across many parameter sets:

- `MODE=0x07` + `GO=1` completes (`GO` self-clears) but `STATUS.DIAG_RESULT = 1`
  ("did not converge") **every time**.
- `STATUS.FB_STS = 1` every time — the auto-resonance engine never locks.
- `A_CAL_COMP` / `A_CAL_BEMF` stay **byte-identical to the L's reset defaults
  (`0x0C` / `0x6C`)** across attempts with `OD_CLAMP` from `0x45` to `0xE0`,
  `BEMF_GAIN` 2→3, `DRIVE_TIME` 24→27 → the routine writes nothing, i.e. it
  aborts before measuring.
- `LRA_PERIOD` (0x22) reads a rock-stable **190 → 53.5 Hz**, which is ≈ **1/3**
  of the true ~158 Hz: the back-EMF zero-crossing detector is counting one
  crossing in three because the Apple actuator's back-EMF is too weak/noisy.

This is a known limitation of DRV2605 + Apple Taptic Engine (Apple drives these
with a dedicated Cirrus/Apple amplifier that uses a separate feedback sense, not
coil back-EMF). **The fix is open-loop drive at a fixed, measured frequency —
which is what the firmware does.**

### 3.2 Tuning — **[measured]**

Found by an open-loop frequency sweep (`drv2605_lra_open_loop_sweep()` in
`Main.c`), driving fixed frequencies and judging the strongest buzz by feel
(no oscilloscope available):

| Parameter | Value | Meaning |
|---|---|---|
| `OL_LRA_PERIOD` (reg 0x20) | **64** | `64 × 98.46 µs = 6301 µs` → **~158.7 Hz**. User felt the peak between ~155 and ~160 Hz; 64 is inside and stable. A ±1 change is ~4 Hz. |
| `OD_CLAMP` (reg 0x17) | **0xC0** | ~4.15 V peak ceiling (`21.59 mV × 192`). Headroom for transient impacts. |
| `CONTROL3` bit 0 | **1** | `LRA_OPEN_LOOP` enabled |
| `MODE` (reg 0x01) | **0x05** | RTP — amplitude comes from reg 0x02 |
| Sustained drive level (`RUMBLE_AMP`, RTP reg 0x02) | **0x48** (72) | → sustained ≈ 4.15 V × 72/127 ≈ **2.35 V** (near the actuator's ~2 V rating → thermally sustainable, see §4). This is the **primary loudness / thermal knob.** |
| Transient impact level (`IMPACT_AMP`) | **0x7F** (127) | full `OD_CLAMP`, only for brief hits |

`RATED_VOLTAGE` (0x16), `DRIVE_TIME` (0x1B), `FB_BRAKE_FACTOR`, `LOOP_GAIN`,
`BEMF_GAIN` in `Main.c` matter only to the **unused** closed-loop / cal paths;
they are kept at plausible values and can be ignored for the product.

### 3.3 Amplitude math

```
output_peak_voltage ≈ (OD_CLAMP × 21.6 mV) × (|RTP| / 127)
```
- `RTP` (reg 0x02) is **signed int8**: `0` = no drive, `+127` = full `OD_CLAMP`,
  negatives drive the opposite phase (used for active braking pulses).
- To change loudness at runtime the PC scales `RTP`; `OD_CLAMP` is set once as
  the ceiling via `CTRL_SET_CONFIG`.

---

## 4. Thermal limits

Apple publishes no spec for this actuator. These figures are the material
physics; treat them as the design envelope.

| Actuator body temperature | Consequence |
|---|---|
| **< ~60 °C** | Safe for continuous operation. "Barely warm" to the touch. |
| 60–80 °C | OK for bursts. Sustained operation here ages the structural adhesive and the magnet. |
| **> ~80–85 °C sustained** | **Damage.** The moving mass is an NdFeB magnet; consumer-grade NdFeB begins **irreversible demagnetisation around 80 °C**, worse the longer it stays hot → **permanent loss of output**. Structural adhesive creeps from ~80–100 °C, especially under vibration. |
| > ~130 °C | Coil enamel (class 130–155) fails → eventual open/short. |

- **Rule of thumb**: if you cannot comfortably hold a fingertip on the actuator
  (~50 °C), the sustained level is too high for continuous use — reduce
  `RUMBLE_AMP` / `OD_CLAMP` or duty-cycle the effect.
- Mechanical: driving well above the ~2 V rating fatigues the spring/flexure
  over time, independent of temperature. Keep the **sustained** level modest;
  reserve high amplitude for short impacts.
- **Mitigations for a rig**: bolt the actuator to a metal bracket (heatsink);
  keep `RUMBLE_AMP` conservative; the **failsafe** (ramp-to-zero on telemetry
  loss, see [PROTOCOL.md](PROTOCOL.md) §5) exists specifically so a host crash
  cannot leave it buzzing.
- **Firmware guard (Phase 6)**: an NTC glued to the actuator → ADC → the
  `haptic_engine` scales `amp_max` down as temperature rises (thermal foldback).
  Until then, `stats.drv_status` exposes the DRV2605's own OVER_TEMP bit (that is
  the *chip* junction, not the actuator — necessary but not sufficient).

---

## 5. USB — composite device (Phase 3)

The box enumerates as **one USB Full-Speed device with two functions**:

| Interface(s) | Function | Endpoints | Host sees |
|---|---|---|---|
| 0 + 1 (IAD) | CDC-ACM | EP4 IN int 8; EP1 OUT/IN bulk 64 | a `/dev/cu.usbmodem*` serial port — **logs only** |
| 2 | vendor (class `0xFF`) | EP2 OUT/IN bulk 64 | a libusb-claimable device — **the framed protocol** ([PROTOCOL.md](PROTOCOL.md)) |

- **IDs**: `idVendor 0x1A86` (WCH), `idProduct 0x5730` — `USB_VID` / `USB_PID`
  in `config.h`. `bDeviceClass 0xEF / 0x02 / 0x01` + an Interface Association
  Descriptor group interfaces 0–1 as the CDC function. bcdUSB **2.00**.
- **Endpoint hardware**: the CH570 USB device has EP0..EP4, each bidirectional,
  64-byte buffers (EP4 shares EP0's RAM region). EP3 is free (RF bridge, §6).
- `src/usb/usb_device.c` owns the peripheral, `USB_IRQHandler`, the descriptors
  and the enumeration state machine (evolved from the bench-proven CDC logger).
  Concurrency model: [ARCHITECTURE.md](ARCHITECTURE.md) §3 — the USB ISR only
  stages/dequeues 64-byte packets; parsing (`link_rx`) runs in the main loop via
  `transport_usb.poll()`.
- **macOS / Linux**: no driver needed. The OS binds its CDC-ACM driver to
  interfaces 0–1; `libusb` claims interface 2. **Windows**: needs a manual
  WinUSB bind (Zadig) until the MS OS 2.0 descriptors land (ROADMAP 3.6).
- ⚠️ **If the device does not enumerate after Phase 3** (regression from the
  working Phase 0–2 CDC): the likely suspects, in order, are (1) bcdUSB 2.00 —
  try 1.10; (2) the IAD / `0xEF` device class — some stacks are picky; (3) EP2
  toggle handling in `USB_DevTransProcess` (it mirrors the proven EP1 path, but
  EP2 is new). The CDC-only path is recoverable by reverting `s_dev_descr` /
  `s_cfg_descr` to a single CDC function.

---

## 6. 2.4 GHz radio (Phase 4 — next hardware revision)

- WCH proprietary 2.4 GHz mode. Reference examples: `EVT/EXAM/RF/RF_Basic`,
  `RF_PHY`, and especially **`RF_UartDongle`** (a USB↔RF bridge — the template
  for `targets/dongle/`).
- TX power −25 dBm … +7 dBm, software-selectable. Channel switch settling ≥ 80 µs.
- `TxBuf` 64 bytes, `RxBuf` 264 bytes (DMA, 4-byte aligned). → protocol frames
  stay **≤ 60 bytes** (already the [PROTOCOL.md](PROTOCOL.md) limit).
- No ARQ in v1: lossy. The link layer's sequence numbers + zero-fill loss policy
  + failsafe handle it. Optionally send each frame twice (still < 15 % air-time
  at 1 kHz) — a config decision for Phase 4.

---

## 7. Power

- VDD = 5 V to both the MCU domain (through its regulator) and the DRV2605L.
- Peak actuator current ≈ `4.15 V / 9.65 Ω ≈ 0.43 A` at full impact; sustained
  ≈ 0.24 A. The 5 V supply and its bulk cap must handle these transients — watch
  `stats.drv_vbat` during a `MODE_LOCAL_TEST` full-drive: a large sag means the
  supply, not the chip, is the limit.
- The DRV2605L output stage is the low-power variant; a phone-sized Taptic
  Engine in its original device is driven by a much larger amplifier. If the
  product ever needs materially more output, that is a driver-IC change
  (DRV2605 non-L, DRV2625, or an external H-bridge / class-D fed by the MCU),
  not a firmware change — and it would move the "sink" behind
  `drv2605_set_amplitude()`.
