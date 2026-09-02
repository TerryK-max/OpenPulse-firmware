#!/usr/bin/env python3
"""OpenPulse software bench (docs/ROADMAP.md 3.5, docs/BENCH.md).

Automated measurements over the USB vendor pipe — no external hardware:
  throughput sweep, frame-rate stress, overload / graceful degradation,
  failsafe latency, PC<->box round-trip, USB write-time distribution.

The end-to-end *analog* latency (actuator actually moving) needs a scope on the
BENCH_GPIO_TRACE pins — see docs/BENCH.md. This script also has a --latency-probe
mode that drives a scope-friendly stream.

    pip install pyusb            # + libusb backend
    python3 bench.py --all
    python3 bench.py --all --out ../../docs/BENCH.md
    python3 bench.py --latency-probe            # for the scope session
"""
from __future__ import annotations
import argparse, os, statistics, sys, threading, time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "pc_sender"))
try:
    import usb.core, usb.util
except ModuleNotFoundError:
    sys.exit("pyusb missing — %s -m pip install pyusb" % sys.executable)
import proto
from openpulse_send import open_box, wave_sine, EP_OUT, EP_IN, VENDOR_IFACE  # noqa


# --------------------------------------------------------------------------- #
class BenchBox:
    def __init__(self, dev):
        self.dev = dev
        self.tx_seq = 0
        self._stats = None
        self._pong = {}
        self._run = False
        self._thr = None

    def send(self, t, payload=b""):
        self.dev.write(EP_OUT, proto.frame(t, self.tx_seq, payload), timeout=1000)
        self.tx_seq = (self.tx_seq + 1) & 0xFF

    def _read1(self, timeout_ms):
        try:
            return bytes(self.dev.read(EP_IN, 64, timeout=timeout_ms))
        except usb.core.USBError:
            return None

    # synchronous request/response (thread must be stopped) -----------------
    def rtt(self, nonce, timeout_ms=200):
        self.send(proto.TYPE_CTRL_PING, bytes([nonce]))
        t0 = time.perf_counter()
        deadline = t0 + timeout_ms / 1000
        while time.perf_counter() < deadline:
            buf = self._read1(50)
            r = proto.parse(buf) if buf else None
            if r and r[0] == proto.TYPE_CTRL_PONG and r[2] and r[2][0] == nonce:
                return time.perf_counter() - t0
        return None

    def status_sync(self, timeout_ms=300):
        self.send(proto.TYPE_STATUS_REQ)
        deadline = time.perf_counter() + timeout_ms / 1000
        while time.perf_counter() < deadline:
            buf = self._read1(50)
            r = proto.parse(buf) if buf else None
            if r and r[0] == proto.TYPE_STATUS_REP:
                return proto.parse_stats(r[2])
        return None

    # background reader (for the streaming tests) --------------------------
    def start_reader(self):
        self._run = True
        self._thr = threading.Thread(target=self._loop, daemon=True)
        self._thr.start()

    def stop_reader(self):
        self._run = False
        if self._thr:
            self._thr.join(timeout=1)
            self._thr = None

    def _loop(self):
        while self._run:
            buf = self._read1(100)
            r = proto.parse(buf) if buf else None
            if not r:
                continue
            if r[0] == proto.TYPE_STATUS_REP:
                self._stats = proto.parse_stats(r[2])
            elif r[0] == proto.TYPE_FAULT:
                fm = proto.parse_fault(r[2])
                print(f"    !! FAULT code={fm['code']} detail={fm['detail']}")

    def last_stats(self):
        return self._stats

    # helpers -------------------------------------------------------------
    def handshake(self, rate, failsafe, reset_stats=True):
        flags = proto.CFG_FLAG_RESET_STATS if reset_stats else 0
        self.send(proto.TYPE_RESYNC)
        self.send(proto.TYPE_CTRL_PING, bytes([0x01]))
        self.send(proto.TYPE_CTRL_SET_CONFIG,
                  proto.config_msg(sample_rate_hz=rate, failsafe_ms=failsafe, flags=flags))
        self.send(proto.TYPE_CTRL_SET_MODE, bytes([proto.MODE_SAMPLES]))
        time.sleep(0.1)              # let the tick rate + config settle

    def idle(self):
        try:
            self.send(proto.TYPE_CTRL_SET_MODE, bytes([proto.MODE_IDLE]))
        except usb.core.USBError:
            pass


def _delta(a, b, k):
    return (b[k] - a[k]) if (a and b) else None


SCOPE_SECTION = """## Analog latency + jitter (scope / logic analyzer)

Not measurable from software — needs a 2+ channel scope or logic analyzer.

**Build:** flash with `BENCH_GPIO_TRACE 1` in `src/config.h`. This makes:

| Pin | Toggles on | Meaning |
|---|---|---|
| **PA4** | each `DATA_SAMPLES` frame handed to the link layer | "samples arrived" |
| **PA10** | each `drv2605_set_amplitude()` I2C write | "actuator command updated" = the render tick |

**Probe:** PA4, PA10, and (3rd channel) one of the **DRV2605 OUT± / actuator
terminals**. Common ground to the board GND.

**Drive:** `python3 bench.py --latency-probe` — streams 1 kHz, 1 sample/frame,
3 ms lookahead, so a fresh sample plays out almost immediately.

**Measure:**

1. **Render jitter** — PA10 period. Expected ~1.000 ms ± a few µs; note the
   spread and the worst outlier. (Compare `tick_backlog_max` from the software
   run.)
2. **rx → playout latency** — a PA4 edge to the next PA10 edge, at 3 ms
   lookahead. Expected ~1–4 ms (USB frame + `transport_usb.poll` + FIFO + tick +
   I2C write). Re-run the probe after raising the lookahead in `bench.py` to see
   it grow ~1:1 with the buffer — that is the latency tax of the safety margin.
3. **I2C write duration** — PA10 edge to the actual SCL/SDA activity settling
   (if the analyzer decodes I2C). Cross-check the [MEASURED] ~126 µs.
4. **driver + LRA mechanical rise** — PA10 edge to the actuator terminal voltage
   changing (electrical, ~instant) and, if an accelerometer is taped on, to the
   actuator physically moving (~10–20 ms, the dominant and unfixable term).
5. **failsafe** — stop `bench.py` (Ctrl-C), trigger on PA4 going idle, measure
   PA4-last-edge to the actuator drive collapsing to 0. Expected ≈
   `failsafe_ms` + `underrun_decay_ms`.

Record scope screenshots + numbers here.

| measurement | value | notes |
|:--|--:|:--|
| PA10 period (render tick) | _±__ µs_ | |
| rx → playout @ 3 ms lookahead | _ ms_ | |
| rx → playout @ 40 ms lookahead | _ ms_ | |
| I2C RTP write duration | _ µs_ | vs 126 µs measured |
| PA10 → actuator voltage | _ µs_ | electrical |
| PA10 → actuator motion | _ ms_ | needs accelerometer; else cite ~15 ms |
| failsafe: last frame → silence | _ ms_ | vs failsafe_ms + decay |
"""


# --------------------------------------------------------------------------- #
#  tests — each returns a list of markdown table rows (strings)
# --------------------------------------------------------------------------- #
def test_throughput(box, rates, dwell, batch, lookahead_ms):
    print("\n== throughput sweep ==")
    rows = ["| rate Hz | frames/s | samples/s | Δunr | Δovr | Δcrc | Δgap | Δrsync | backlog | verdict |",
            "|--:|--:|--:|--:|--:|--:|--:|--:|--:|:--|"]
    box.start_reader()
    for rate in rates:
        box.handshake(rate, 100)
        la = int(lookahead_ms * rate / 1000)
        gen = wave_sine
        i = 0
        while i < la:                                   # prime
            box.send(proto.TYPE_DATA_SAMPLES, bytes(gen(i + k, rate) for k in range(batch)))
            i += batch
            time.sleep(0.001)
        time.sleep(0.1)
        s0 = box.last_stats()
        t0 = time.perf_counter()
        nstatus = t0
        while time.perf_counter() - t0 < dwell:
            now = time.perf_counter()
            target = int((now - t0) * rate) + la
            while i < target:
                box.send(proto.TYPE_DATA_SAMPLES, bytes(gen(i + k, rate) for k in range(batch)))
                i += batch
            if now >= nstatus:
                box.send(proto.TYPE_STATUS_REQ); nstatus += 0.2
            time.sleep(0.001)
        time.sleep(0.15)
        s1 = box.last_stats()
        fr = _delta(s0, s1, "frames_rx")
        sp = _delta(s0, s1, "samples_played")
        unr = _delta(s0, s1, "fifo_underrun"); ovr = _delta(s0, s1, "fifo_overrun")
        crc = _delta(s0, s1, "crc_err"); gap = _delta(s0, s1, "seq_gap_frames")
        rsy = _delta(s0, s1, "resync")
        bl = s1["tick_backlog_max"] if s1 else None
        ok = (unr == 0 and ovr == 0 and crc == 0)
        rows.append(f"| {rate} | {fr/dwell:.0f} | {sp/dwell:.0f} | {unr} | {ovr} | {crc} | "
                    f"{gap} | {rsy} | {bl} | {'clean ✅' if ok else 'LOSS ⚠️'} |")
        print(f"  {rate:5d} Hz: {sp/dwell:.0f} samp/s  unr={unr} ovr={ovr} crc={crc} gap={gap} backlog={bl}")
        box.idle(); time.sleep(0.3)
    box.stop_reader()
    return rows


def test_overload(box, seconds, batch):
    print("\n== overload / graceful degradation ==")
    box.start_reader()
    box.handshake(2000, 100)
    gen = wave_sine
    i = 0
    s0 = box.status_sync() or box.last_stats()
    t0 = time.perf_counter()
    sent = 0
    while time.perf_counter() - t0 < seconds:           # blast, no pacing
        box.send(proto.TYPE_DATA_SAMPLES, bytes(gen(i + k, 2000) for k in range(batch)))
        i += batch; sent += 1
    dur = time.perf_counter() - t0
    time.sleep(0.2)
    alive = box.status_sync() is not None               # still answers control?
    s1 = box.last_stats()
    box.idle(); box.stop_reader()
    rows = ["", f"Blasted **{sent/dur:.0f} frames/s** ({batch} samp/frame) for {dur:.1f}s, no pacing.",
            "", "| metric | value |", "|:--|--:|",
            f"| box still answers STATUS_REQ | {'yes ✅' if alive else 'NO ⚠️'} |",
            f"| Δcrc_err (must be 0) | {_delta(s0, s1, 'crc_err')} |",
            f"| Δbad_type (must be 0) | {_delta(s0, s1, 'bad_type')} |",
            f"| Δfifo_overrun | {_delta(s0, s1, 'fifo_overrun')} |",
            f"| Δseq_gap_frames | {_delta(s0, s1, 'seq_gap_frames')} |",
            f"| Δresync | {_delta(s0, s1, 'resync')} |",
            f"| samples_played monotonic | {'yes ✅' if s1 and s0 and s1['samples_played'] >= s0['samples_played'] else '?'} |"]
    print("  " + rows[1])
    return rows


def test_failsafe(box, reps, failsafe_ms):
    print("\n== failsafe latency ==")
    box.start_reader()
    lat = []
    for _ in range(reps):
        box.handshake(1000, failsafe_ms)
        gen = wave_sine
        i = 0
        t0 = time.perf_counter()
        while time.perf_counter() - t0 < 2.0:
            target = int((time.perf_counter() - t0) * 1000) + 40
            while i < target:
                box.send(proto.TYPE_DATA_SAMPLES, bytes(gen(i + k, 1000) for k in range(8)))
                i += 8
            time.sleep(0.001)
        base = (box.last_stats() or {}).get("failsafe_trips", 0)
        t_last = time.perf_counter()
        hit = None
        box.stop_reader()                                # measure synchronously
        while time.perf_counter() - t_last < 1.0:
            st = box.status_sync(60)
            if st and st["failsafe_trips"] > base:
                hit = time.perf_counter() - t_last
                break
            time.sleep(0.005)
        box.start_reader()
        if hit is not None:
            lat.append(hit * 1000)
            print(f"  trip {hit*1000:.0f} ms after last frame")
        box.idle(); time.sleep(0.3)
    box.stop_reader()
    if not lat:
        return ["", "failsafe: **not observed** ⚠️"]
    return ["", f"failsafe_ms configured = **{failsafe_ms}**; observed trip latency over {len(lat)} runs: "
            f"min **{min(lat):.0f}** / mean **{statistics.mean(lat):.0f}** / max **{max(lat):.0f}** ms "
            f"(STATUS poll granularity ~5–60 ms)."]


def test_rtt(box, reps):
    print("\n== PC <-> box round-trip (PING/PONG over EP2) ==")
    box.stop_reader()
    for _ in range(20):
        box._read1(20)                                   # drain
    rtt = []
    for n in range(reps):
        r = box.rtt((n % 250) + 1)
        if r is not None:
            rtt.append(r * 1000)
    if not rtt:
        return ["", "RTT: **no PONG** ⚠️"]
    rtt.sort()
    p = lambda q: rtt[min(len(rtt) - 1, int(q * len(rtt)))]
    print(f"  n={len(rtt)} min={rtt[0]:.2f} p50={p(.5):.2f} p95={p(.95):.2f} max={rtt[-1]:.2f} ms")
    return ["", f"PING→PONG over the vendor pipe, n={len(rtt)}: min **{rtt[0]:.2f}** / "
            f"p50 **{p(.5):.2f}** / p95 **{p(.95):.2f}** / max **{rtt[-1]:.2f}** ms. "
            f"Includes USB out + one box main-loop iteration + USB in."]


def test_usb_write(box, n):
    print("\n== USB bulk-OUT write time (from Python) ==")
    box.stop_reader()
    dt = []
    payload = bytes(range(8))
    for _ in range(n):
        t = time.perf_counter()
        box.send(proto.TYPE_DATA_SAMPLES, payload)
        dt.append((time.perf_counter() - t) * 1e6)
    dt.sort()
    p = lambda q: dt[min(len(dt) - 1, int(q * len(dt)))]
    print(f"  n={n} min={dt[0]:.0f} p50={p(.5):.0f} p95={p(.95):.0f} max={dt[-1]:.0f} µs")
    return ["", f"`dev.write()` for a 12-byte frame, n={n}: min **{dt[0]:.0f}** / p50 **{p(.5):.0f}** / "
            f"p95 **{p(.95):.0f}** / max **{dt[-1]:.0f}** µs."]


def latency_probe(box, seconds, lookahead_ms):
    """Scope-friendly stream: batch=1, 1 kHz. Watch PA4 (per frame) vs PA10 (per
    actuator update) vs a probe on the DRV2605 output."""
    la = max(1, lookahead_ms)
    print(f"latency-probe: 1 kHz, batch=1, {la} ms lookahead, {seconds:.0f}s. "
          f"Scope PA4 / PA10 / DRV output — see docs/BENCH.md §2.")
    box.handshake(1000, 100)
    gen = wave_sine
    i = 0
    while i < la:
        box.send(proto.TYPE_DATA_SAMPLES, bytes([gen(i, 1000)])); i += 1
    t0 = time.perf_counter()
    try:
        while time.perf_counter() - t0 < seconds:
            target = int((time.perf_counter() - t0) * 1000) + la
            while i < target:
                box.send(proto.TYPE_DATA_SAMPLES, bytes([gen(i, 1000)])); i += 1
            time.sleep(0.0005)
    except KeyboardInterrupt:
        pass
    box.idle()


# --------------------------------------------------------------------------- #
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--all", action="store_true", help="run every software test")
    ap.add_argument("--throughput", action="store_true")
    ap.add_argument("--overload", action="store_true")
    ap.add_argument("--failsafe", action="store_true")
    ap.add_argument("--rtt", action="store_true")
    ap.add_argument("--usb-write", action="store_true")
    ap.add_argument("--latency-probe", type=float, nargs="?", const=60.0, default=None,
                    metavar="SECONDS", help="drive a scope-friendly stream and exit")
    ap.add_argument("--rates", default="500,1000,1500,2000,3000,4000")
    ap.add_argument("--dwell", type=float, default=8.0, help="seconds per throughput step")
    ap.add_argument("--batch", type=int, default=8)
    ap.add_argument("--lookahead-ms", type=int, default=None,
                    help="PC lookahead (default 40 for sweeps, 3 for --latency-probe)")
    ap.add_argument("--out", help="write the markdown report here (e.g. ../../docs/BENCH.md)")
    ap.add_argument("--vid", type=lambda x: int(x, 0), default=0x1A86)
    ap.add_argument("--pid", type=lambda x: int(x, 0), default=0x5730)
    a = ap.parse_args()

    box = BenchBox(open_box(a.vid, a.pid))

    if a.latency_probe is not None:
        latency_probe(box, a.latency_probe, a.lookahead_ms if a.lookahead_ms is not None else 3)
        return

    if not any([a.all, a.throughput, a.overload, a.failsafe, a.rtt, a.usb_write]):
        ap.error("pick a test (--all, --throughput, ...) or --latency-probe")

    box.start_reader()
    info = box.status_sync() or {}
    box.stop_reader()
    hdr = [f"# OpenPulse bench results",
           "",
           f"- date: {time.strftime('%Y-%m-%d %H:%M')}",
           f"- firmware: fw {info.get('fw_major','?')}.{info.get('fw_minor','?')}  "
           f"proto v{info.get('proto_version','?')}",
           f"- host: {sys.platform}, python {sys.version.split()[0]}",
           f"- generated by `tools/bench/bench.py {' '.join(sys.argv[1:])}`",
           ""]
    out = list(hdr)
    rates = [int(x) for x in a.rates.split(",")]
    la = a.lookahead_ms if a.lookahead_ms is not None else 40

    if a.all or a.throughput:
        out += ["## Throughput sweep", ""] + test_throughput(box, rates, a.dwell, a.batch, la) + [""]
    if a.all or a.overload:
        out += ["## Overload / graceful degradation"] + test_overload(box, 4.0, a.batch) + [""]
    if a.all or a.failsafe:
        out += ["## Failsafe latency"] + test_failsafe(box, 5, 100) + [""]
    if a.all or a.rtt:
        out += ["## PC ↔ box round-trip"] + test_rtt(box, 200) + [""]
    if a.all or a.usb_write:
        out += ["## USB bulk-OUT write time"] + test_usb_write(box, 500) + [""]

    out.append(SCOPE_SECTION)

    text = "\n".join(out) + "\n"
    if a.out:
        with open(a.out, "w") as f:
            f.write(text)
        print(f"\nwrote {a.out}")
    else:
        print("\n" + "=" * 70 + "\n" + text)


if __name__ == "__main__":
    main()
