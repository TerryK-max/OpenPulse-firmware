/******************************************************************************
 * haptic_fifo.h — single-producer / single-consumer ring of int8 samples.
 *
 * Header only, lock-free. Contract (docs/ARCHITECTURE.md §3):
 *   - exactly ONE producer  (Phase 1: the local generator in the main loop;
 *                             Phase 3: the USB/RF rx ISR)
 *   - exactly ONE consumer   (the main loop, in haptic_tick())
 *   - single RV32 hart: aligned 32-bit head/tail loads/stores are atomic, and
 *     an ISR sees the main loop's stores in program order and vice-versa, so a
 *     compiler barrier is sufficient — no disable-interrupts, no fences.
 *   - overrun  (producer catches consumer): push() returns 0, DON'T overwrite.
 *   - underrun (consumer catches producer): pop() returns 0.
 *
 * Capacity is a compile-time power of two (HAPTIC_FIFO_CAP, config.h).
 *****************************************************************************/
#ifndef HAPTIC_FIFO_H
#define HAPTIC_FIFO_H

#include <stdint.h>
#include "config.h"                  /* HAPTIC_FIFO_CAP */

#ifndef HAPTIC_FIFO_CAP
#define HAPTIC_FIFO_CAP  128u        /* ~128 ms at 1 kHz */
#endif
#if (HAPTIC_FIFO_CAP & (HAPTIC_FIFO_CAP - 1u)) != 0u
#error "HAPTIC_FIFO_CAP must be a power of two"
#endif

typedef struct {
    volatile uint32_t head;          /* producer advances */
    volatile uint32_t tail;          /* consumer advances */
    int8_t   buf[HAPTIC_FIFO_CAP];
} haptic_fifo_t;

#define HAPTIC_FIFO_BARRIER()  __asm__ volatile("" ::: "memory")

static inline void haptic_fifo_reset(haptic_fifo_t *f)
{
    f->head = 0;
    f->tail = 0;
}

/* head - tail is the fill level; wraps correctly in uint32 as long as the ring
 * never holds > 2^32 entries (it holds <= HAPTIC_FIFO_CAP). */
static inline uint32_t haptic_fifo_count(const haptic_fifo_t *f)
{
    return f->head - f->tail;
}

static inline uint32_t haptic_fifo_space(const haptic_fifo_t *f)
{
    return HAPTIC_FIFO_CAP - (f->head - f->tail);
}

/* Producer only. @return 1 pushed, 0 full. */
static inline int haptic_fifo_push(haptic_fifo_t *f, int8_t v)
{
    uint32_t head = f->head;
    if ((head - f->tail) >= HAPTIC_FIFO_CAP) return 0;
    f->buf[head & (HAPTIC_FIFO_CAP - 1u)] = v;
    HAPTIC_FIFO_BARRIER();                /* publish data before the new head */
    f->head = head + 1u;
    return 1;
}

/* Consumer only. @return 1 + *out, 0 empty. */
static inline int haptic_fifo_pop(haptic_fifo_t *f, int8_t *out)
{
    uint32_t tail = f->tail;
    if ((f->head - tail) == 0u) return 0;
    *out = f->buf[tail & (HAPTIC_FIFO_CAP - 1u)];
    HAPTIC_FIFO_BARRIER();
    f->tail = tail + 1u;
    return 1;
}

#endif /* HAPTIC_FIFO_H */
