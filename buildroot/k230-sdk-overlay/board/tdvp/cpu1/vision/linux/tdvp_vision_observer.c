/* SPDX-License-Identifier: MIT */
#include "tdvp_vision_observer.h"

void tdvp_vision_observe(struct tdvp_vision_observer *observer,
                         const struct tdvp_vision_producer *sample,
                         tdvp_v_u64 expected_epoch, tdvp_v_u64 now_ms)
{
    if (!expected_epoch || !sample || sample->epoch != expected_epoch) {
        *observer = (struct tdvp_vision_observer){0};
        return;
    }
    if (observer->sample.epoch != expected_epoch)
        *observer = (struct tdvp_vision_observer){0};
    if (sample->magic != TDVP_VISION_MAGIC || sample->version != TDVP_VISION_ABI_VERSION ||
        sample->control_bytes != sizeof(struct tdvp_vision_control) ||
        sample->slot_count != TDVP_VISION_SLOT_COUNT || sample->slot_bytes != TDVP_VISION_SLOT_BYTES ||
        sample->state < TDVP_VISION_STATE_IDLE || sample->state > TDVP_VISION_STATE_FAULT ||
        !sample->heartbeat || (observer->seen &&
        (now_ms < observer->last_ms || sample->heartbeat < observer->sample.heartbeat))) {
        observer->protocol_fault = 1;
        return;
    }
    if (!observer->seen) {
        observer->seen = 1;
        observer->seen_ms = now_ms;
    } else if (sample->heartbeat > observer->sample.heartbeat) {
        observer->advanced = 1;
        observer->seen_ms = now_ms;
    }
    observer->last_ms = now_ms;
    observer->sample = *sample;
}

unsigned int tdvp_vision_observer_state(const struct tdvp_vision_observer *observer,
                                       tdvp_v_u64 now_ms)
{
    if (observer->protocol_fault || (observer->seen && now_ms < observer->last_ms))
        return TDVP_OBSERVER_PROTOCOL;
    if (!observer->seen)
        return TDVP_OBSERVER_PENDING;
    if (now_ms - observer->seen_ms >= TDVP_OBSERVER_TIMEOUT_MS)
        return TDVP_OBSERVER_STALE;
    if (observer->sample.fault || observer->sample.state == TDVP_VISION_STATE_FAULT)
        return TDVP_VISION_STATE_FAULT;
    if (!observer->advanced)
        return TDVP_OBSERVER_PENDING;
    return observer->sample.state;
}

const char *tdvp_vision_observer_name(unsigned int state)
{
    switch (state) {
    case TDVP_VISION_STATE_IDLE: return "idle";
    case TDVP_VISION_STATE_STARTING: return "starting";
    case TDVP_VISION_STATE_RUNNING: return "running";
    case TDVP_VISION_STATE_STOPPING: return "stopping";
    case TDVP_VISION_STATE_FAULT: return "fault";
    case TDVP_OBSERVER_STALE: return "stale";
    case TDVP_OBSERVER_PROTOCOL: return "protocol-error";
    default: return "pending";
    }
}
