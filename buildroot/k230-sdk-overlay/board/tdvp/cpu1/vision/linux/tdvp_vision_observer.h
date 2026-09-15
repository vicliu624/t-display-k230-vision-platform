/* SPDX-License-Identifier: MIT */
#ifndef TDVP_VISION_OBSERVER_H
#define TDVP_VISION_OBSERVER_H
#include "tdvp_vision_abi.h"

#define TDVP_OBSERVER_PENDING 0U
#define TDVP_OBSERVER_STALE 6U
#define TDVP_OBSERVER_PROTOCOL 7U
#define TDVP_OBSERVER_TIMEOUT_MS 10000U

/* Read-only telemetry, NOT a frame lease or an ownership authorization. */
struct tdvp_vision_observer {
    struct tdvp_vision_producer sample;
    tdvp_v_u64 seen_ms, last_ms;
    unsigned int seen, advanced, protocol_fault;
};
void tdvp_vision_observe(struct tdvp_vision_observer *, const struct tdvp_vision_producer *,
                         tdvp_v_u64 expected_epoch, tdvp_v_u64 now_ms);
unsigned int tdvp_vision_observer_state(const struct tdvp_vision_observer *, tdvp_v_u64 now_ms);
const char *tdvp_vision_observer_name(unsigned int state);
#endif
