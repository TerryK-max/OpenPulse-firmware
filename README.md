# OpenPulse — firmware

A small USB / 2.4 GHz **haptic-feedback box for sim racing**. A PC mixes every
effect and sound texture into a single amplitude waveform and streams it in tiny
packets; the box is a thin renderer that drives one actuator.

This repository is the **box firmware**. Runs on a WCH CH570D.

- **MCU** — WCH **CH570D** (CH572/CH570 family, 32-bit RISC-V, USB Full-Speed,
  2.4 GHz radio), 100 MHz.
- **Haptic driver** — TI **DRV2605L** over I²C.
- **Actuator** — a salvaged Apple iPhone XS Max "Taptic Engine" (an LRA), driven
  **open-loop** at its ~158.7 Hz resonance (closed-loop auto-calibration does not
  converge with this actuator — see [docs/HARDWARE.md](docs/HARDWARE.md)).
- **Transports** — USB now, a paired 2.4 GHz dongle at the next board revision;
  both feed the same protocol parser.
- **Sample rate** — 1–2 kHz amplitude samples.

## Status

| Phase | State |
|---|---|
| 0 — modular refactor + standalone build | done |
| 1 — haptic engine, SPSC FIFO, SysTick tick, failsafe | done, bench-confirmed |
| 2 — transport-agnostic link layer + wire protocol v1 | done (host tests green) |
| 3 — USB composite device (CDC + vendor bulk) + Python PC sender | done, bench check pending |
| 4 — 2.4 GHz transport + dongle | needs the next board revision |
| 5–6 — high-rate PWM path (optional), production hardening | planned |

Full plan with acceptance criteria: [docs/ROADMAP.md](docs/ROADMAP.md).

## Build

The WCH SDK and evaluation package are **not** in this repo (their copyright).
Get them in place first — **[docs/SETUP.md](docs/SETUP.md)** — then:

```bash
make check      # every src/ TU compiles clean at -Wall -Wextra
make            # full firmware -> build/CH570D.hex
make test       # host unit tests for the link layer (no toolchain needed)
```

MounRiver Studio users: open `CH570D.wvproj`. See [docs/BUILD.md](docs/BUILD.md).

Drive the box from a PC (Phase 3):

```bash
cd tools/pc_sender && pip install -r requirements.txt
python3 openpulse_send.py --wave sim --duration 20
```

## Documentation

Start at **[docs/README.md](docs/README.md)** — it is written so an independent
contributor (or an AI agent) can pick the project up cold. From there:
[ARCHITECTURE](docs/ARCHITECTURE.md) · [PROTOCOL](docs/PROTOCOL.md) ·
[HARDWARE](docs/HARDWARE.md) · [ROADMAP](docs/ROADMAP.md) · [BUILD](docs/BUILD.md).

## Licence

Apache-2.0 for the project's own code — see [LICENSE](LICENSE) and [NOTICE](NOTICE).
Third-party components are listed in [THIRD_PARTY.md](THIRD_PARTY.md) and are
under their own terms.
