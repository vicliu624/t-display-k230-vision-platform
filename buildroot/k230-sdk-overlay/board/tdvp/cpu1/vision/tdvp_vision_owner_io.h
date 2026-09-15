/* SPDX-License-Identifier: MIT */
#ifndef TDVP_VISION_OWNER_IO_H
#define TDVP_VISION_OWNER_IO_H
#include "tdvp_vision_owner.h"
#include <stddef.h>

/* RT-Smart/noncached adapter. Linux must use its readl/writel primitives,
 * with the same sequence protocol; never cast __iomem to normal memory.
 */
static inline void tdvp_owner_fence(void)
{
    __sync_synchronize();
}

static inline int tdvp_owner_snapshot(const volatile struct tdvp_owner_record *wire,
                                      struct tdvp_owner_record *snapshot)
{
    tdvp_v_u32 sequence = wire->sequence;
    unsigned int i;
    if (sequence & 1U) return 0;
    tdvp_owner_fence();
    snapshot->magic = wire->magic;
    snapshot->version = wire->version;
    snapshot->bytes = wire->bytes;
    snapshot->contract = wire->contract;
    snapshot->cookie = wire->cookie;
    snapshot->peer_cookie = wire->peer_cookie;
    snapshot->heartbeat = wire->heartbeat;
    snapshot->state = wire->state;
    snapshot->fault = wire->fault;
    snapshot->sequence = wire->sequence;
    for (i = 0; i < 19; ++i) snapshot->reserved[i] = wire->reserved[i];
    tdvp_owner_fence();
    return wire->sequence == sequence && snapshot->sequence == sequence;
}

static inline void tdvp_owner_publish(volatile struct tdvp_owner_record *wire,
                                     const struct tdvp_owner_record *record)
{
    tdvp_v_u32 sequence = (wire->sequence + 1U) | 1U;
    unsigned int i;
    wire->sequence = sequence;
    tdvp_owner_fence();
    wire->magic = record->magic;
    wire->version = record->version;
    wire->bytes = record->bytes;
    wire->contract = record->contract;
    wire->cookie = record->cookie;
    wire->peer_cookie = record->peer_cookie;
    wire->heartbeat = record->heartbeat;
    wire->state = record->state;
    wire->fault = record->fault;
    for (i = 0; i < 19; ++i) wire->reserved[i] = record->reserved[i];
    tdvp_owner_fence();
    wire->sequence = sequence + 1U;
    tdvp_owner_fence();
}
#endif
