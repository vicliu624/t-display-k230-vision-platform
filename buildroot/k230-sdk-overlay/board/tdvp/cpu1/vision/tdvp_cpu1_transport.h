/* SPDX-License-Identifier: MIT */
#ifndef TDVP_CPU1_TRANSPORT_H
#define TDVP_CPU1_TRANSPORT_H
#include "tdvp_cpu1_capture.h"
#include "tdvp_vision_abi.h"

struct tdvp_cpu1_transport {
    volatile struct tdvp_vision_control *control;
    uint8_t *slots; /* uncached mapping, exactly 3 * SLOT_BYTES */
    uint64_t epoch;
    uint64_t published;
    uint64_t released;
};

/* init is for a new firmware instance only, before Linux attaches. It must
 * never be used to discard unread frames or recover a stalled consumer. */
int tdvp_cpu1_transport_init(struct tdvp_cpu1_transport *transport,
                             volatile struct tdvp_vision_control *control,
                             void *slots, uint64_t epoch);
int tdvp_cpu1_transport_publish(const struct tdvp_cpu1_frame *frame, void *context);
void tdvp_cpu1_transport_state(struct tdvp_cpu1_transport *transport, uint32_t state, int fault);

#endif
