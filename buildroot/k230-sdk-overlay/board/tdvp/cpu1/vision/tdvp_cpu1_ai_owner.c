/* SPDX-License-Identifier: MIT */
#include "tdvp_cpu1_ai_service.h"
#include "tdvp_vision_owner_io.h"
#include "tdvp_cpu1_shared_map.h"
#include "tdvp_ai_abi.h"

int tdvp_cpu1_ai_owner_check(struct tdvp_ai_owner_view *view, uint64_t now)
{
    const volatile struct tdvp_owner_control *control = view->mapping;
    struct tdvp_owner_record record;
    int stable = 0;
    if (!control || !now || !view->owner_cookie || !view->peer_cookie) return -EINVAL;
    for (unsigned int attempt = 0; attempt < 4; ++attempt)
        if ((stable = tdvp_owner_snapshot(&control->cpu1_side, &record))) break;
    if (stable) {
        if (record.magic != TDVP_OWNER_MAGIC || record.version != TDVP_OWNER_VERSION ||
            record.bytes != sizeof(record) || record.contract != TDVP_OWNER_CONTRACT ||
            record.state != TDVP_OWNER_READY || record.fault ||
            record.cookie != view->owner_cookie || record.peer_cookie != view->peer_cookie ||
            !record.heartbeat || record.heartbeat < view->heartbeat) return -EPIPE;
        if (record.heartbeat != view->heartbeat) {
            view->heartbeat = record.heartbeat;
            view->seen_ms = now;
        }
    }
    if (!view->seen_ms || now < view->seen_ms || now - view->seen_ms >= TDVP_OWNER_PEER_MS)
        return -ETIMEDOUT;
    return 0;
}

void *tdvp_cpu1_ai_map_buffer(unsigned int output)
{
    if (output > 1) { errno = EINVAL; return MAP_FAILED; }
    return tdvp_cpu1_shared_map(output ? TDVP_AI_OUTPUT_BASE : TDVP_AI_INPUT_BASE, TDVP_AI_BUFFER_BYTES);
}
