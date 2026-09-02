/******************************************************************************
 * proto.h — the FROZEN PC <-> box wire contract.  Protocol v1.
 *
 *   >>> KEEP THIS FILE BYTE-IDENTICAL WITH  tools/proto/proto.h  <<<
 *
 * Authoritative spec: docs/PROTOCOL.md. Any change to frame layout, TYPE_*
 * values, struct field order/size/meaning, or the CRC requires bumping
 * PROTO_VERSION and updating: this file, tools/proto/proto.h, docs/PROTOCOL.md
 * and the PC sender — all in the same commit (docs/README.md rule 3).
 *
 * Frame:  [TYPE][SEQ][LEN][PAYLOAD x LEN][CRC8]      total = LEN + 4, range 4..60
 *   CRC8 = CRC-8/SMBUS (poly 0x07, init 0x00, no reflection, no final XOR)
 *          over bytes [0 .. 3+LEN-1]  (TYPE, SEQ, LEN, PAYLOAD).
 * All multi-byte integers are little-endian (x86 and the RV32 core agree).
 *****************************************************************************/
#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>

#define PROTO_VERSION           1

#define PROTO_MAX_PAYLOAD       56
#define PROTO_FRAME_OVERHEAD    4                                  /* TYPE+SEQ+LEN+CRC8 */
#define PROTO_MAX_FRAME         (PROTO_MAX_PAYLOAD + PROTO_FRAME_OVERHEAD)  /* 60 */
#define PROTO_MIN_FRAME         PROTO_FRAME_OVERHEAD                        /* 4  */

/* Frame byte offsets. */
#define PROTO_OFF_TYPE          0
#define PROTO_OFF_SEQ           1
#define PROTO_OFF_LEN           2
#define PROTO_OFF_PAYLOAD       3

/* A SEQ gap of this many frames or more is treated as a resync (docs/PROTOCOL.md
 * §2 "SEQ and loss"). */
#define PROTO_RESYNC_GAP        16

/* ---- message types ------------------------------------------------------ */
enum {
    /* PC -> box */
    TYPE_DATA_SAMPLES    = 0x01,   /* payload: int8 samples, N = LEN            */
    TYPE_ENVELOPE        = 0x02,   /* payload: struct env_cmd                   */
    TYPE_CTRL_SET_CONFIG = 0x10,   /* payload: struct config_msg               */
    TYPE_CTRL_SET_MODE   = 0x11,   /* payload: [mode:u8]                        */
    TYPE_CTRL_PING       = 0x12,   /* payload: [nonce:u8]                       */
    TYPE_STATUS_REQ      = 0x20,   /* payload: (empty)                         */
    TYPE_RESYNC          = 0x7F,   /* payload: (empty) - flush FIFO, reseat SEQ */

    /* box -> PC */
    TYPE_CTRL_PONG       = 0x81,   /* payload: struct pong_msg                  */
    TYPE_STATUS_REP      = 0x82,   /* payload: struct stats_msg                 */
    TYPE_LOG             = 0x83,   /* payload: [level:u8][ascii, no NUL]        */
    TYPE_FAULT           = 0x84,   /* payload: [code:u8][detail:u32 LE]         */
};

/* ---- MODE_* (payload of TYPE_CTRL_SET_MODE; mirrors haptic_mode_t) ------- */
enum {
    MODE_IDLE       = 0,
    MODE_SAMPLES    = 1,
    MODE_ENVELOPE   = 2,
    MODE_LOCAL_TEST = 3,
};

/* ---- FAULT codes (payload byte 0 of TYPE_FAULT) ------------------------- */
enum {
    FAULT_BAD_CONFIG = 1,   /* detail = 0                                */
    FAULT_BAD_MODE   = 2,   /* detail = offending mode byte             */
    FAULT_VERSION    = 3,   /* detail = box PROTO_VERSION               */
    FAULT_I2C        = 4,   /* detail = i2c error count since boot      */
    FAULT_DRV_OT     = 5,   /* detail = DRV2605 STATUS reg              */
    FAULT_INTERNAL   = 6,   /* detail = assert/site id                  */
};

/* ---- LOG levels (payload byte 0 of TYPE_LOG) --------------------------- */
enum { PROTO_LOG_ERR = 0, PROTO_LOG_WARN = 1, PROTO_LOG_INFO = 2, PROTO_LOG_DBG = 3 };
#define PROTO_LOG_MAX_TEXT   55

/* ---- config_msg field ranges (docs/PROTOCOL.md §4.3) -------------------
 * A field value of 0 means "keep current" for every field that has a unit;
 * loss_policy and flags are taken verbatim. reserved must be 0. */
#define PROTO_RATE_MIN         250
#define PROTO_RATE_MAX         4000
#define PROTO_FAILSAFE_MIN     10
#define PROTO_FAILSAFE_MAX     2000
#define PROTO_OLP_MAX          127      /* reg 0x20 is [6:0] */
#define PROTO_AMP_MAX          127
#define PROTO_CFG_FLAG_PERSIST     0x01 /* bit0: persist to DataFlash (Phase 6) */
#define PROTO_CFG_FLAG_RESET_STATS 0x02 /* bit1: zero the STATUS_REP counters */
#define PROTO_CFG_FLAG_MASK        0x03 /* bits outside this must be 0 */

/* ---- payload structs -------------------------------------------------- */

struct __attribute__((packed)) env_cmd {   /* TYPE_ENVELOPE — 2 bytes */
    uint8_t target;      /* 0..255 target amplitude (255 = full) */
    uint8_t slew_ms;     /* 0 = jump; 1..255 = linear ramp time  */
};

struct __attribute__((packed)) config_msg {  /* TYPE_CTRL_SET_CONFIG — 12 bytes */
    uint8_t  version;            /* must == PROTO_VERSION            */
    uint8_t  flags;              /* PROTO_CFG_FLAG_*                 */
    uint8_t  ol_lra_period;      /* DRV2605 reg 0x20; 0 = keep       */
    uint8_t  od_clamp;           /* DRV2605 reg 0x17; 0 = keep       */
    uint16_t sample_rate_hz;     /* 250..4000; 0 = keep             */
    uint16_t failsafe_ms;        /* 10..2000; 0 = keep             */
    uint8_t  amp_max;            /* 1..127;   0 = keep             */
    uint8_t  loss_policy;        /* 0 = zero-fill, 1 = hold-last     */
    uint8_t  underrun_decay_ms;  /* 1..255;   0 = keep             */
    uint8_t  reserved;           /* must be 0                       */
};

struct __attribute__((packed)) pong_msg {   /* TYPE_CTRL_PONG — 5 bytes */
    uint8_t nonce;               /* echoed from the PING payload */
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t proto_version;       /* == PROTO_VERSION */
    uint8_t mode;                /* current MODE_*  */
};

struct __attribute__((packed)) stats_msg {  /* TYPE_STATUS_REP — 40 bytes */
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
    uint16_t tick_backlog_max;
    uint16_t fifo_fill;
    uint8_t  drv_status;         /* last DRV2605 STATUS reg  */
    uint8_t  drv_vbat;           /* last DRV2605 VBAT reg; VDD ~= v * 5.6/255 */
    uint16_t reserved;
};

struct __attribute__((packed)) fault_msg {  /* TYPE_FAULT — 5 bytes */
    uint8_t  code;
    uint32_t detail;
};

/* RV32 / x86 pack these naturally; assert it so a compiler change cannot
 * silently move the wire format. */
_Static_assert(sizeof(struct env_cmd)    ==  2, "env_cmd must be 2 bytes");
_Static_assert(sizeof(struct config_msg) == 12, "config_msg must be 12 bytes");
_Static_assert(sizeof(struct pong_msg)   ==  5, "pong_msg must be 5 bytes");
_Static_assert(sizeof(struct stats_msg)  == 40, "stats_msg must be 40 bytes");
_Static_assert(sizeof(struct fault_msg)  ==  5, "fault_msg must be 5 bytes");

/* CRC-8/SMBUS over n bytes. Implementation: src/util/crc.c. */
uint8_t proto_crc8(const uint8_t *p, uint16_t n);

#endif /* PROTO_H */
