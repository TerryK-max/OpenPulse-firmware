# Wire Protocol — v1 (FROZEN)

Read [README.md](README.md) and [ARCHITECTURE.md](ARCHITECTURE.md) first.

This is the contract between the **PC** (mixer / sender) and the **box**
(renderer). It is carried unchanged over **USB** today and over **2.4 GHz** at
the next hardware revision.

> **`PROTO_VERSION = 1`.**
> Any change to: frame layout, `TYPE_*` values, struct field order/size/meaning,
> or CRC — **requires** bumping `PROTO_VERSION`, updating this document, updating
> `src/link/proto.h` **and** its byte-identical copy `tools/proto/proto.h`,
> updating the PC sender, all in the same commit. There is no backward
> compatibility mechanism in v1; version mismatch = hard refuse (see §7).

---

## 1. Transport model

| Transport | Delivery unit | Notes |
|---|---|---|
| USB — vendor **bulk OUT** (PC→box) | one bulk transfer = **one frame** | 64-byte max packet on FS; a frame ≤ 60 bytes fits in one packet. |
| USB — vendor **bulk IN** (box→PC) | one bulk transfer = **one frame** | PONG / STATUS_REP / LOG / FAULT. |
| USB — **CDC-ACM** interface | raw bytes, **not framed** | Human-readable logs only, and an optional text control fallback. Not part of this protocol. |
| RF 2.4 GHz (Phase 4) | one radio packet = **one frame** | `EVT/EXAM/RF` TxBuf is 64 bytes → same ≤ 60-byte frame limit. Lossy: no ARQ in v1. |

**One frame per delivery unit.** No frame spans two packets. No two frames share
a packet.

All multi-byte integer fields are **little-endian** (matches x86 and the RV32
core).

**USB is live (Phase 3):** vendor interface 2, `EP2 OUT` (0x02) / `EP2 IN`
(0x82), `idVendor:idProduct` `1A86:5730`. Firmware side: `src/usb/usb_vendor.h`
+ `src/transport/transport_usb.c`. Host side: `tools/pc_sender/` (pyusb). The
CDC interfaces (0–1) are a plain serial port for logs and never carry frames.

---

## 2. Frame layout

```
 byte  field       description
 ────  ──────────  ─────────────────────────────────────────────────────────
  0    TYPE        message type (§3)
  1    SEQ         u8, increments per frame per direction, wraps 255→0
  2    LEN         u8, payload length in bytes, 0..56
 3..   PAYLOAD     LEN bytes, meaning depends on TYPE
 3+LEN CRC8        over bytes [0 .. 3+LEN-1]  (TYPE, SEQ, LEN, PAYLOAD)
```

- **Total frame size** = `LEN + 4`, range 4..60 bytes.
- **CRC8**: polynomial `0x07` (CRC-8/SMBUS), init `0x00`, no reflection, no final
  XOR. Reference implementation lives in `src/util/crc.c`. Verified on every
  received frame; a mismatch increments `stats.crc_err` and the frame is
  dropped silently (do **not** NACK — keeps the box stateless and cheap).

### SEQ and loss

- Two independent counters: PC→box and box→PC.
- Receiver keeps `last_seq`. On a good frame, `gap = (SEQ - last_seq - 1) & 0xFF`.
  - `gap == 0`: normal.
  - `gap in 1..15`: that many frames were lost. Increment
    `stats.seq_gap_frames += gap`. For **DATA_SAMPLES**, apply the loss policy
    (§4.1). For control frames, ignore the gap (they are idempotent or re-sent).
  - `gap >= 16` (or a `RESYNC` was seen): treat as a resync — clear the FIFO,
    set `last_seq = SEQ`, ramp output to 0, `stats.resync++`.
- The box **never blocks or retransmits**. Lossy-but-live always beats
  reliable-but-late for haptics.

---

## 3. Message types

```c
/* PC -> box */
#define TYPE_DATA_SAMPLES   0x01   /* payload: int8 samples, N = LEN            */
#define TYPE_ENVELOPE       0x02   /* payload: struct env_cmd                   */
#define TYPE_CTRL_SET_CONFIG 0x10  /* payload: struct config_msg               */
#define TYPE_CTRL_SET_MODE  0x11   /* payload: [mode:u8]                        */
#define TYPE_CTRL_PING      0x12   /* payload: [nonce:u8]                       */
#define TYPE_STATUS_REQ     0x20   /* payload: (empty)                          */
#define TYPE_RESYNC         0x7F   /* payload: (empty) - flush FIFO, reseat SEQ */

/* box -> PC */
#define TYPE_CTRL_PONG      0x81   /* payload: struct pong_msg                  */
#define TYPE_STATUS_REP     0x82   /* payload: struct stats_msg                 */
#define TYPE_LOG            0x83   /* payload: [level:u8][ascii, no NUL]        */
#define TYPE_FAULT          0x84   /* payload: [code:u8][detail:u32 LE]         */
```

Unknown `TYPE` on the box → drop, `stats.bad_type++`, and emit one
`TYPE_LOG`(WARN) (rate-limited).

---

## 4. Payloads

### 4.1 `TYPE_DATA_SAMPLES` (0x01)

- Payload = `LEN` signed bytes. Each is one amplitude **sample**:
  `-127..+127`, where `0` = no drive and the sign selects the drive phase
  (PC normally sends `0..127`; negatives are for explicit anti-phase braking).
- The box appends all samples to `haptic_fifo`, then plays them out at
  `config.sample_rate_hz`, one per tick.
- **Batching**: the PC sends `LEN = sample_rate_hz / packet_rate`. E.g. at
  1 kHz sample rate and one packet per USB frame (1 kHz) → `LEN = 1`; at one
  packet per 8 ms → `LEN = 8`. Keep `LEN ≤ 56`.
- **Loss policy** (`config.loss_policy`) applied for the `gap` missing frames,
  estimating `gap × recent_LEN` missing samples:
  - `0` (default) **zero-fill**: insert that many `0` samples → the actuator
    goes quiet through the gap. Safe, slightly "notchy".
  - `1` **hold-last**: repeat the last sample → smoother, but a lost "stop"
    command means it keeps buzzing until the next frame or the failsafe.
- **FIFO overrun / underrun**: see [ARCHITECTURE.md](ARCHITECTURE.md) §3.

### 4.2 `TYPE_ENVELOPE` (0x02)

```c
struct env_cmd {         /* 2 bytes */
    uint8_t target;      /* 0..255 target amplitude (255 = full)   */
    uint8_t slew_ms;     /* 0 = jump; 1..255 = linear ramp time ms */
};
```
- Alternative to sample streaming: the box ramps its output from the current
  level to `target` over `slew_ms`, stepping once per tick. Tiny (6-byte frame),
  very loss-tolerant — **recommended for the RF transport** and for slowly
  varying effects.
- `DATA_SAMPLES` and `ENVELOPE` must not be interleaved within one mode; the PC
  picks one via `CTRL_SET_MODE`.

### 4.3 `TYPE_CTRL_SET_CONFIG` (0x10)

```c
struct config_msg {              /* 12 bytes, little-endian */
    uint8_t  version;            /* must == PROTO_VERSION            */
    uint8_t  flags;              /* bit0: persist to DataFlash (Phase 6)
                                   bit1..7: reserved, send 0        */
    uint8_t  ol_lra_period;      /* DRV2605 reg 0x20; 0 = keep       */
    uint8_t  od_clamp;           /* DRV2605 reg 0x17; 0 = keep       */
    uint16_t sample_rate_hz;     /* 250..4000; 0 = keep             */
    uint16_t failsafe_ms;        /* 10..2000; 0 = keep (default 100) */
    uint8_t  amp_max;            /* hard clamp on |sample|, 0..127; 0 = keep */
    uint8_t  loss_policy;        /* 0 = zero-fill, 1 = hold-last     */
    uint8_t  underrun_decay_ms;  /* 1..255; 0 = keep (default 20)    */
    uint8_t  reserved;           /* send 0                           */
};
```
- Box validates every field against its allowed range. On any failure it replies
  `TYPE_FAULT` with `code = FAULT_BAD_CONFIG` and applies **nothing**.
- On success it applies the changes (re-runs `drv2605_lra_configure()` if
  `ol_lra_period`/`od_clamp` changed, reprograms SysTick if `sample_rate_hz`
  changed) and replies `TYPE_STATUS_REP`.
- `ol_lra_period` maps to frequency as `f = 1e6 / (ol_lra_period × 98.46)` Hz.
  The bench value today is **64 → ~158.7 Hz** (see [HARDWARE.md](HARDWARE.md)).

### 4.4 `TYPE_CTRL_SET_MODE` (0x11)

```c
#define MODE_IDLE        0   /* output forced to 0, FIFO drained/ignored     */
#define MODE_SAMPLES     1   /* consume TYPE_DATA_SAMPLES                     */
#define MODE_ENVELOPE    2   /* consume TYPE_ENVELOPE                         */
#define MODE_LOCAL_TEST  3   /* run haptic_patterns, ignore incoming data    */
```
Payload = `[mode:u8]`. Invalid mode → `TYPE_FAULT` / `FAULT_BAD_MODE`.
Switching mode always ramps through 0 first.

### 4.5 `TYPE_CTRL_PING` (0x12) → `TYPE_CTRL_PONG` (0x81)

```c
struct pong_msg {          /* 5 bytes */
    uint8_t nonce;         /* echoed from the PING payload */
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t proto_version; /* == PROTO_VERSION */
    uint8_t mode;          /* current MODE_*  */
};
```
The PC should PING at least once after connect and treat
`proto_version != its own` as fatal (§7).

### 4.6 `TYPE_STATUS_REQ` (0x20) → `TYPE_STATUS_REP` (0x82)

```c
struct stats_msg {              /* 40 bytes, little-endian */
    uint8_t  fw_major, fw_minor, proto_version, mode;
    uint32_t uptime_s;
    uint32_t frames_rx;
    uint32_t samples_played;
    uint16_t crc_err;
    uint16_t bad_type;
    uint16_t seq_gap_frames;
    uint16_t resync;
    uint16_t fifo_overrun;
    uint16_t fifo_underrun;
    uint16_t i2c_err;
    uint16_t failsafe_trips;
    uint16_t tick_backlog_max;   /* max (g_tick advance) seen between loop iters */
    uint16_t fifo_fill;          /* current samples buffered */
    uint8_t  drv_status;         /* last DRV2605 STATUS reg (bit1 OVER_TEMP, bit0 OC) */
    uint8_t  drv_vbat;           /* last DRV2605 VBAT reg; VDD ≈ v × 5.6 / 255 */
    uint16_t reserved;
};
```
The PC polls this at ~5–10 Hz for a live dashboard. This is the primary "how is
the box doing" channel during streaming; text `LOG` is for rare events only.

### 4.7 `TYPE_LOG` (0x83)  (box → PC)

Payload = `[level:u8]` (`0=ERR 1=WARN 2=INFO 3=DBG`) followed by ASCII text,
**no trailing NUL**, `LEN-1` characters, `≤ 55` chars. Emitted by the `log/`
module when its sink is the link (see [ARCHITECTURE.md](ARCHITECTURE.md) §2,
`log`). Never emitted from an ISR. Rate-limited by the log ring drain.

### 4.8 `TYPE_FAULT` (0x84)  (box → PC)

```
payload: [code:u8][detail:u32 LE]
```
```c
#define FAULT_BAD_CONFIG    1   /* detail = 0 */
#define FAULT_BAD_MODE      2   /* detail = offending mode byte */
#define FAULT_VERSION       3   /* detail = box PROTO_VERSION */
#define FAULT_I2C           4   /* detail = i2c error count since boot */
#define FAULT_DRV_OT        5   /* DRV2605 OVER_TEMP latched; detail = STATUS reg */
#define FAULT_INTERNAL      6   /* detail = assert/site id */
```
A `FAULT` does not stop the box unless it is `FAULT_DRV_OT` (which forces
`MODE_IDLE` until a `CTRL_SET_MODE` clears it).

### 4.9 `TYPE_RESYNC` (0x7F)

Empty payload. Box: clear FIFO, ramp to 0, set `last_seq` from this frame's SEQ,
`stats.resync++`. The PC sends this after any gap it detects in its own send
path, or after (re)connecting.

---

## 5. Failsafe (mandatory box behaviour)

- The box timestamps the arrival of every **valid** `DATA_SAMPLES` / `ENVELOPE`
  frame (`last_data_ms`).
- Each main-loop iteration: if `MODE ∈ {SAMPLES, ENVELOPE}` and
  `time_now_ms() - last_data_ms > config.failsafe_ms`, force the output to ramp
  to 0 over `underrun_decay_ms`, stay there, and `stats.failsafe_trips++`.
  Normal streaming resumes automatically on the next valid frame.
- USB bus reset / disconnect, or (Phase 4) RF link-lost → immediate ramp to 0.
- This exists because a stuck-on actuator overheats — see
  [HARDWARE.md](HARDWARE.md) §"Thermal limits".

---

## 6. Typical session

```
PC → CTRL_PING(nonce)                     box → CTRL_PONG(nonce, fw, proto, mode)
PC → CTRL_SET_CONFIG(version=1, sample_rate_hz=1000, failsafe_ms=100, ...)
                                          box → STATUS_REP(...)
PC → CTRL_SET_MODE(MODE_SAMPLES)          box → STATUS_REP(...)  [mode now 1]
PC → DATA_SAMPLES[seq=0]  (1..8 samples per frame, one frame per ms)
PC → DATA_SAMPLES[seq=1]
...
        (every ~150 ms, in parallel)
PC → STATUS_REQ                            box → STATUS_REP(...)   [dashboard]
        (on quit)
PC → CTRL_SET_MODE(MODE_IDLE)              box → STATUS_REP(...)   [output 0]
```

---

## 7. Versioning and mismatch handling

- `proto.h` defines `#define PROTO_VERSION 1`.
- The box replies with its `PROTO_VERSION` in `CTRL_PONG` and validates
  `config_msg.version` in `CTRL_SET_CONFIG`.
- If a `CTRL_SET_CONFIG` carries a different `version`: reply
  `TYPE_FAULT(FAULT_VERSION, box_version)`, **ignore all `DATA_*` frames** until
  a matching `CTRL_SET_CONFIG` arrives. Stay in `MODE_IDLE`.
- The PC, on `PONG.proto_version != its own` or on `FAULT_VERSION`, must stop and
  surface a clear "firmware/PC protocol mismatch (box vN, PC vM)" error. No
  guessing, no partial operation.
- v1 has **no** in-band negotiation or optional fields. When the protocol needs
  to evolve, either bump to v2 (and the PC speaks the version the box reports) or
  add a `TYPE_CTRL_CAPS` exchange as the first v2 feature.

---

## 8. `proto.h` skeleton  —  IMPLEMENTED (Phase 2, 2026-09-01)

Live at [`src/link/proto.h`](../src/link/proto.h), byte-identical copy at
`tools/proto/proto.h` (`make test` `diff`s them). The real header adds, without
changing the wire format: `struct fault_msg` (the `[code:u8][detail:u32]`
payload), config field-range constants (`PROTO_RATE_MIN` … used by
`link_control`), frame-offset macros (`PROTO_OFF_*`), `PROTO_RESYNC_GAP`,
`PROTO_LOG_*` level names, and `_Static_assert` on all five struct sizes. The
parser/dispatcher is `src/link/link.c`; `CTRL_*` handlers are
`src/link/link_control.c`; CRC-8 is `src/util/crc.c`. Behaviour is covered by
`tools/test/` (83 checks).

The skeleton below is the original design sketch, kept for reference:

```c
/* src/link/proto.h  — keep byte-identical with tools/proto/proto.h  */
#ifndef PROTO_H
#define PROTO_H
#include <stdint.h>

#define PROTO_VERSION            1
#define PROTO_MAX_PAYLOAD        56
#define PROTO_FRAME_OVERHEAD     4          /* TYPE + SEQ + LEN + CRC8 */
#define PROTO_MAX_FRAME          (PROTO_MAX_PAYLOAD + PROTO_FRAME_OVERHEAD)

enum {
    TYPE_DATA_SAMPLES = 0x01, TYPE_ENVELOPE = 0x02,
    TYPE_CTRL_SET_CONFIG = 0x10, TYPE_CTRL_SET_MODE = 0x11, TYPE_CTRL_PING = 0x12,
    TYPE_STATUS_REQ = 0x20, TYPE_RESYNC = 0x7F,
    TYPE_CTRL_PONG = 0x81, TYPE_STATUS_REP = 0x82, TYPE_LOG = 0x83, TYPE_FAULT = 0x84,
};
enum { MODE_IDLE = 0, MODE_SAMPLES = 1, MODE_ENVELOPE = 2, MODE_LOCAL_TEST = 3 };
enum { FAULT_BAD_CONFIG=1, FAULT_BAD_MODE=2, FAULT_VERSION=3,
       FAULT_I2C=4, FAULT_DRV_OT=5, FAULT_INTERNAL=6 };

struct __attribute__((packed)) env_cmd    { uint8_t target, slew_ms; };
struct __attribute__((packed)) config_msg { uint8_t version, flags, ol_lra_period, od_clamp;
                                            uint16_t sample_rate_hz, failsafe_ms;
                                            uint8_t amp_max, loss_policy, underrun_decay_ms, reserved; };
struct __attribute__((packed)) pong_msg   { uint8_t nonce, fw_major, fw_minor, proto_version, mode; };
struct __attribute__((packed)) stats_msg  { uint8_t fw_major, fw_minor, proto_version, mode;
                                            uint32_t uptime_s, frames_rx, samples_played;
                                            uint16_t crc_err, bad_type, seq_gap_frames, resync,
                                                     fifo_overrun, fifo_underrun, i2c_err,
                                                     failsafe_trips, tick_backlog_max, fifo_fill;
                                            uint8_t drv_status, drv_vbat; uint16_t reserved; };

uint8_t proto_crc8(const uint8_t *p, uint16_t n);   /* poly 0x07, init 0x00 */
#endif
```

Verify `sizeof(struct config_msg) == 12` and `sizeof(struct stats_msg) == 40` in
a static assert; RV32 packing of these layouts is natural but assert it anyway.
