"""OpenPulse wire protocol v1 — Python mirror of src/link/proto.h.

Keep in sync with src/link/proto.h (docs/PROTOCOL.md). This is hand-maintained;
if you change proto.h, change this too. Frame:

    [TYPE][SEQ][LEN][PAYLOAD x LEN][CRC8]      total = LEN + 4, range 4..60
    CRC8 = CRC-8/SMBUS (poly 0x07, init 0x00), over TYPE..last payload byte.
"""
from __future__ import annotations
import struct

PROTO_VERSION = 1
PROTO_MAX_PAYLOAD = 56
PROTO_MAX_FRAME = PROTO_MAX_PAYLOAD + 4
PROTO_RESYNC_GAP = 16

# message types --------------------------------------------------------------
TYPE_DATA_SAMPLES = 0x01
TYPE_ENVELOPE = 0x02
TYPE_CTRL_SET_CONFIG = 0x10
TYPE_CTRL_SET_MODE = 0x11
TYPE_CTRL_PING = 0x12
TYPE_STATUS_REQ = 0x20
TYPE_RESYNC = 0x7F
TYPE_CTRL_PONG = 0x81
TYPE_STATUS_REP = 0x82
TYPE_LOG = 0x83
TYPE_FAULT = 0x84

TYPE_NAME = {v: k for k, v in globals().items() if k.startswith("TYPE_")}

# modes ---------------------------------------------------------------------
MODE_IDLE, MODE_SAMPLES, MODE_ENVELOPE, MODE_LOCAL_TEST = 0, 1, 2, 3

# fault codes -------------------------------------------------------------
FAULT_BAD_CONFIG, FAULT_BAD_MODE, FAULT_VERSION, FAULT_I2C, FAULT_DRV_OT, FAULT_INTERNAL = range(1, 7)

LOG_LEVEL = {0: "ERR", 1: "WARN", 2: "INFO", 3: "DBG"}


def crc8_smbus(data: bytes) -> int:
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if (c & 0x80) else (c << 1) & 0xFF
    return c


def frame(type_: int, seq: int, payload: bytes = b"") -> bytes:
    assert len(payload) <= PROTO_MAX_PAYLOAD, "payload too long"
    body = bytes([type_ & 0xFF, seq & 0xFF, len(payload)]) + payload
    return body + bytes([crc8_smbus(body)])


def parse(buf: bytes):
    """Return (type, seq, payload) or None if the frame is malformed."""
    if len(buf) < 4 or len(buf) > PROTO_MAX_FRAME:
        return None
    plen = buf[2]
    if plen + 4 != len(buf):
        return None
    if crc8_smbus(buf[:-1]) != buf[-1]:
        return None
    return buf[0], buf[1], buf[3:3 + plen]


# payload structs (little-endian, packed) --------------------------------
# config_msg: version, flags, ol_lra_period, od_clamp,
#             sample_rate_hz:u16, failsafe_ms:u16,
#             amp_max, loss_policy, underrun_decay_ms, reserved            (12 B)
_CONFIG = struct.Struct("<BBBBHHBBBB")
# pong_msg: nonce, fw_major, fw_minor, proto_version, mode                  (5 B)
_PONG = struct.Struct("<BBBBB")
# stats_msg (40 B) — see docs/PROTOCOL.md §4.6
_STATS = struct.Struct("<BBBB III HHHHHHHHHH BBH")


def config_msg(sample_rate_hz=0, failsafe_ms=0, ol_lra_period=0, od_clamp=0,
               amp_max=0, loss_policy=0, underrun_decay_ms=0, flags=0):
    return _CONFIG.pack(PROTO_VERSION, flags, ol_lra_period, od_clamp,
                        sample_rate_hz, failsafe_ms, amp_max, loss_policy,
                        underrun_decay_ms, 0)


def parse_pong(p: bytes) -> dict:
    n, maj, mnr, pv, mode = _PONG.unpack(p)
    return dict(nonce=n, fw="%d.%d" % (maj, mnr), proto=pv, mode=mode)


_STATS_FIELDS = ("fw_major fw_minor proto_version mode uptime_s frames_rx "
                 "samples_played crc_err bad_type seq_gap_frames resync "
                 "fifo_overrun fifo_underrun i2c_err failsafe_trips "
                 "tick_backlog_max fifo_fill drv_status drv_vbat reserved").split()


def parse_stats(p: bytes) -> dict:
    return dict(zip(_STATS_FIELDS, _STATS.unpack(p)))


def parse_fault(p: bytes) -> dict:
    code, detail = struct.unpack("<BI", p)
    return dict(code=code, detail=detail)


assert _CONFIG.size == 12 and _PONG.size == 5 and _STATS.size == 40, "struct size drift vs proto.h"
assert crc8_smbus(b"123456789") == 0xF4, "CRC-8/SMBUS self-test"
