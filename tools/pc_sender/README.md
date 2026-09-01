# tools/pc_sender

Reference PC-side sender for the OpenPulse box (docs/ROADMAP.md Phase 3.4).
Streams an amplitude waveform over the USB **vendor bulk** pipe and prints the
box's STATUS dashboard. Not the final integration — that will read game
telemetry (SimHub etc.) — just enough to bench-test the link end to end.

## Setup

A venv keeps this off whichever system Python you have (macOS often has
several — anaconda, python.org, `/usr/bin` — and `pip` vs `python3` can point at
different ones):

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
```

Without a venv, install with the **same** interpreter that runs the script:

```bash
python3 -m pip install pyusb        # NOT just `pip install`
python3 openpulse_send.py --list
```

pyusb needs a **libusb** backend: macOS `brew install libusb`, Debian/Ubuntu
`sudo apt install libusb-1.0-0`.

macOS/Linux need no driver for the vendor interface (class 0xFF); the OS keeps
the CDC interfaces (the `/dev/cu.usbmodem*` log port) for itself. On Linux you
may need a udev rule or `sudo` to claim the interface.

## Use

```bash
python3 openpulse_send.py --list                 # find the box
python3 openpulse_send.py --wave sim --duration 20
python3 openpulse_send.py --wave sine --rate 1000
```

It does the handshake (PING → SET_CONFIG → SET_MODE(SAMPLES) → RESYNC), streams
`DATA_SAMPLES` frames paced to real time (`--batch` samples per frame), polls
`STATUS_REQ` at 10 Hz, and prints one dashboard line per second. Ctrl-C sends
`SET_MODE(IDLE)` and exits — the box's failsafe would also silence it ~100 ms
after the stream stops.

While this runs, the CDC log (`screen /dev/cu.usbmodem* 115200`) shows the box
side: `[R]` lines with `USB[HOST ...]` once the sender takes over from the
local self-test.

## Files

| File | What |
|---|---|
| `proto.py` | Hand-maintained mirror of `src/link/proto.h` (frame layout, CRC-8, struct packing). Change both together. |
| `openpulse_send.py` | The sender + a background reader thread for box→PC frames. |
