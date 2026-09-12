/* SPDX-License-Identifier: MIT */
#ifndef TDVP_CPU1_AI_SERVICE_H
#define TDVP_CPU1_AI_SERVICE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Once per boot-lifetime vision process, after ownership + transport setup.
 * Threads retain all mappings/storage after fault. No exit/restart recovery. */
int tdvp_cpu1_ai_service_start(void *vision_control, void *ownership,
                              uint64_t owner_cookie, uint64_t peer_cookie);
struct tdvp_ai_owner_view {
    const volatile void *mapping;
    uint64_t owner_cookie, peer_cookie, heartbeat, seen_ms;
};
/* Each execution/supervisor thread has its own view: no shared C data race. */
int tdvp_cpu1_ai_owner_check(struct tdvp_ai_owner_view *view, uint64_t now);
void *tdvp_cpu1_ai_map_buffer(unsigned int output);
#ifdef __cplusplus
}
#endif
#endif
