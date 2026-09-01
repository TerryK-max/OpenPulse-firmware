/******************************************************************************
 * transport.h — the transport abstraction (docs/ARCHITECTURE.md §2 layer 5).
 *
 * A transport binds a byte pipe (USB vendor bulk now, 2.4 GHz RF at the next
 * board revision) to the link layer:
 *   inbound whole frames  -> link_rx()
 *   link outbound frames  -> the pipe
 *
 * Exactly one transport is active. The link layer never sees which pipe
 * delivered the bytes (docs/PROTOCOL.md §1).
 *****************************************************************************/
#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>

typedef struct {
    void    (*init)(void);          /* wire link <-> pipe (calls link_init)   */
    void    (*poll)(void);          /* main-loop pump: deliver staged inbound */
    uint8_t (*host_active)(void);    /* 1 once a peer is actually sending      */
} transport_t;

#endif /* TRANSPORT_H */
