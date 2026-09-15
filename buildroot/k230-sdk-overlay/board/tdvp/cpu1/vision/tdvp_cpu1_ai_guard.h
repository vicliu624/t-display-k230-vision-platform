/* SPDX-License-Identifier: MIT */
#ifndef TDVP_CPU1_AI_GUARD_H
#define TDVP_CPU1_AI_GUARD_H
#include <stdint.h>
#include <poll.h>
#ifdef __cplusplus
extern "C" {
#endif

/* One supervisor and one execution thread per guard. Begin is called only
 * before starting/joining the execution thread; deadline never changes while
 * active. Negative status is terminal for this guard's process lifetime.
 * The guard does NOT stop DMA, cancel threads, free buffers or reset hardware.
 */
enum { TDVP_AI_IDLE = 0, TDVP_AI_ACTIVE = 1, TDVP_AI_DONE = 2 };
/* Pinned lwp_syscall.c dfs2musl_events expands one DFS POLLIN into
 * POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND. This is not POLLERR. */
#define TDVP_AI_RTSMART_POLLIN 0x00c3
struct tdvp_cpu1_ai_guard {
    int status;
    uint64_t started_ms, deadline_ms;
};
struct tdvp_cpu1_ai_guard_ops {
    int (*now)(void *context, uint64_t *ms);
    int (*ready)(void *context);
    int (*poll)(void *context, struct pollfd *fds, nfds_t count, int timeout);
    /* Must yield without releasing the job's storage. Even if this returns,
     * wait() keeps parking forever after a fault; it never returns to nncase.
     * A separate supervisor publishes the error and rejects further work. */
    void (*park)(void *context);
    void *context;
};
int tdvp_cpu1_ai_guard_status(const struct tdvp_cpu1_ai_guard *guard);
int tdvp_cpu1_ai_guard_begin(struct tdvp_cpu1_ai_guard *guard, uint64_t now, unsigned int budget_ms);
int tdvp_cpu1_ai_guard_fail(struct tdvp_cpu1_ai_guard *guard, int error);
int tdvp_cpu1_ai_guard_check(struct tdvp_cpu1_ai_guard *guard, const struct tdvp_cpu1_ai_guard_ops *ops);
int tdvp_cpu1_ai_guard_finish(struct tdvp_cpu1_ai_guard *guard, const struct tdvp_cpu1_ai_guard_ops *ops);
int tdvp_cpu1_ai_guard_wait(struct tdvp_cpu1_ai_guard *guard, const struct tdvp_cpu1_ai_guard_ops *ops,
                          int expected_fd, struct pollfd *fds, nfds_t count);
#ifdef __cplusplus
}
#endif
#endif
