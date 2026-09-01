#!/usr/bin/env python3
"""OpenPulse reference PC sender (docs/ROADMAP.md 3.4).

Streams an amplitude waveform to the box over the USB vendor-bulk pipe and
prints the box's STATUS dashboard + log lines. This is a reference / bench
tool, not the final integration (that will read game telemetry).

    pip install pyusb           # needs libusb (brew install libusb / apt install libusb-1.0-0)
    python3 openpulse_send.py --list
    python3 openpulse_send.py --wave sim --duration 20
    python3 openpulse_send.py --wave sine --rate 1000

Ctrl-C stops the stream cleanly (SET_MODE IDLE).
"""
from __future__ import annotations
import argparse, math, sys, threading, time

try:
    import usb.core, usb.util
except ModuleNotFoundError:
    sys.exit(
        "pyusb is not installed for this interpreter (%s).\n"
        "Install it with the SAME python that runs this script:\n"
        "    %s -m pip install pyusb\n"
        "or use a venv:\n"
        "    python3 -m venv .venv && source .venv/bin/activate && pip install -r requirements.txt\n"
        "(pyusb also needs a libusb backend: `brew install libusb` / `apt install libusb-1.0-0`)"
        % (sys.executable, sys.executable)
    )

import proto

VID, PID = 0x1A86, 0x5730
EP_OUT, EP_IN = 0x02, 0x82
VENDOR_IFACE = 2


# --------------------------------------------------------------------------- #
#  waveform generators — return int8 in 0..127, one call per sample index
# --------------------------------------------------------------------------- #
def wave_sim(i: int, rate: int) -> int:
    """Mirrors the firmware's haptic_pattern_simracing feel: modulated rumble
    with periodic kerb impacts."""
    ms = i * 1000 // rate
    lvl = 0x48
    wob = (ms // 250) % 4
    lvl += -10 if wob == 3 else wob * 7
    lvl += ((ms // 10) * 7) % 11 - 5
    if ms % 1500 < 30:
        lvl = 0x7F
    return max(0, min(0x7F, lvl))


def wave_sine(i: int, rate: int) -> int:
    return int(63 + 63 * math.sin(2 * math.pi * 1.0 * i / rate))   # 1 Hz envelope


WAVES = {"sim": wave_sim, "sine": wave_sine}


# --------------------------------------------------------------------------- #
class Box:
    def __init__(self, dev):
        self.dev = dev
        self.tx_seq = 0
        self.rx_seq = None
        self._stop = threading.Event()
        self._last_stats = None
        self._rx = threading.Thread(target=self._reader, daemon=True)

    # -- framing --
    def send(self, type_, payload=b""):
        f = proto.frame(type_, self.tx_seq, payload)
        self.tx_seq = (self.tx_seq + 1) & 0xFF
        self.dev.write(EP_OUT, f, timeout=1000)

    # -- inbound reader thread --
    def _reader(self):
        while not self._stop.is_set():
            try:
                buf = bytes(self.dev.read(EP_IN, 64, timeout=200))
            except usb.core.USBError as e:
                if e.errno in (110, None) or "timeout" in str(e).lower():
                    continue
                if self._stop.is_set():
                    return
                print("  [rx error]", e); continue
            r = proto.parse(buf)
            if r is None:
                print("  [rx] malformed frame", buf.hex()); continue
            t, seq, pl = r
            self._dispatch(t, seq, pl)

    def _dispatch(self, t, seq, pl):
        if t == proto.TYPE_STATUS_REP:
            self._last_stats = proto.parse_stats(pl)
        elif t == proto.TYPE_CTRL_PONG:
            p = proto.parse_pong(pl)
            print(f"  PONG  nonce=0x{p['nonce']:02X} fw {p['fw']} proto v{p['proto']} mode {p['mode']}")
        elif t == proto.TYPE_LOG:
            lvl = proto.LOG_LEVEL.get(pl[0], "?") if pl else "?"
            print(f"  LOG/{lvl}: {pl[1:].decode('ascii', 'replace')}")
        elif t == proto.TYPE_FAULT:
            fm = proto.parse_fault(pl)
            name = {v: k for k, v in vars(proto).items() if k.startswith("FAULT_")}.get(fm["code"], "?")
            print(f"  !! FAULT {name} detail={fm['detail']}")
        else:
            print(f"  [rx] {proto.TYPE_NAME.get(t, hex(t))} seq={seq} len={len(pl)}")

    # -- lifecycle --
    def start(self):
        self._rx.start()

    def stop(self):
        self._stop.set()
        self._rx.join(timeout=1)

    def dashboard(self):
        s = self._last_stats
        if not s:
            return "  (no STATUS yet)"
        vbat = s["drv_vbat"] * 5.6 / 255
        return ("  up={uptime_s}s rx={frames_rx} played={samples_played} "
                "fifo={fifo_fill} ovr={fifo_overrun} unr={fifo_underrun} "
                "backlog={tick_backlog_max} crc={crc_err} gap={seq_gap_frames} "
                "rsync={resync} bad={bad_type} i2c_err={i2c_err} fs={failsafe_trips} "
                "STAT=0x{drv_status:02X} VBAT~{vbat:.2f}V").format(vbat=vbat, **s)


# --------------------------------------------------------------------------- #
def open_box(vid, pid):
    dev = usb.core.find(idVendor=vid, idProduct=pid)
    if dev is None:
        sys.exit(f"device {vid:#06x}:{pid:#06x} not found (is the box plugged in and flashed?)")
    # Only the vendor interface (2); leave CDC (0,1) to the OS serial driver.
    try:
        if dev.is_kernel_driver_active(VENDOR_IFACE):
            dev.detach_kernel_driver(VENDOR_IFACE)
    except (NotImplementedError, usb.core.USBError):
        pass
    try:
        dev.set_configuration()
    except usb.core.USBError:
        pass                       # already configured by the OS
    usb.util.claim_interface(dev, VENDOR_IFACE)
    return dev


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="list matching USB devices and exit")
    ap.add_argument("--wave", choices=WAVES, default="sim")
    ap.add_argument("--rate", type=int, default=1000, help="sample rate Hz (250..4000)")
    ap.add_argument("--batch", type=int, default=8, help="samples per DATA_SAMPLES frame")
    ap.add_argument("--lookahead-ms", type=int, default=40,
                    help="how far ahead of realtime to keep the box FIFO filled "
                         "(< HAPTIC_FIFO_CAP/rate; the box FIFO is 128 samples)")
    ap.add_argument("--duration", type=float, default=0.0, help="seconds (0 = until Ctrl-C)")
    ap.add_argument("--failsafe", type=int, default=100, help="failsafe_ms")
    ap.add_argument("--vid", type=lambda x: int(x, 0), default=VID)
    ap.add_argument("--pid", type=lambda x: int(x, 0), default=PID)
    a = ap.parse_args()

    if a.list:
        found = list(usb.core.find(find_all=True, idVendor=a.vid))
        for d in found:
            print(f"  {d.idVendor:#06x}:{d.idProduct:#06x}  {usb.util.get_string(d, d.iProduct)}")
        if not found:
            print("  (none)")
        return

    dev = open_box(a.vid, a.pid)
    box = Box(dev)
    box.start()

    # -- handshake (docs/PROTOCOL.md §6) -- RESYNC FIRST so the box's SEQ
    # tracking is reseated before anything is gap-accounted against the
    # not-yet-suspended local self-test.
    box.send(proto.TYPE_RESYNC)
    box.send(proto.TYPE_CTRL_PING, bytes([0xA5]))
    box.send(proto.TYPE_CTRL_SET_CONFIG,
             proto.config_msg(sample_rate_hz=a.rate, failsafe_ms=a.failsafe))
    box.send(proto.TYPE_CTRL_SET_MODE, bytes([proto.MODE_SAMPLES]))

    gen = WAVES[a.wave]
    lookahead = int(a.lookahead_ms * a.rate / 1000)   # samples to keep buffered
    print(f"streaming '{a.wave}' at {a.rate} Hz, {a.batch}/frame, "
          f"{a.lookahead_ms} ms lookahead — Ctrl-C to stop")

    # Prime the FIFO before the paced loop so the box never plays out an empty
    # buffer while the stream spins up. Samples queued before the box finishes
    # the mode switch just wait in the FIFO — no loss.
    i = 0
    while i < lookahead:
        box.send(proto.TYPE_DATA_SAMPLES, bytes(gen(i + k, a.rate) for k in range(a.batch)))
        i += a.batch
        time.sleep(0.001)          # let the box drain its rx ring between frames

    t0 = time.perf_counter()
    next_status = t0
    next_print = t0
    try:
        while a.duration == 0.0 or (time.perf_counter() - t0) < a.duration:
            now = time.perf_counter()
            # keep the box FIFO ~lookahead ahead of what it has consumed by now
            target = int((now - t0) * a.rate) + lookahead
            while i < target:
                s = bytes(gen(i + k, a.rate) for k in range(a.batch))
                box.send(proto.TYPE_DATA_SAMPLES, s)
                i += a.batch
            if now >= next_status:
                box.send(proto.TYPE_STATUS_REQ)
                next_status += 0.1
            if now >= next_print:
                print(box.dashboard())
                next_print += 1.0
            time.sleep(0.002)
    except KeyboardInterrupt:
        print("\nstopping")
    finally:
        try:
            box.send(proto.TYPE_CTRL_SET_MODE, bytes([proto.MODE_IDLE]))
        except usb.core.USBError:
            pass
        box.stop()
        usb.util.release_interface(dev, VENDOR_IFACE)


if __name__ == "__main__":
    main()
