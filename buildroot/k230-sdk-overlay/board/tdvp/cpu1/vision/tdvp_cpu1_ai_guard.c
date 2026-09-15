/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "tdvp_cpu1_ai_guard.h"
#include <errno.h>
#include <time.h>

int tdvp_cpu1_ai_guard_status(const struct tdvp_cpu1_ai_guard *guard)
{
    return __atomic_load_n(&guard->status, __ATOMIC_ACQUIRE);
}

int tdvp_cpu1_ai_guard_fail(struct tdvp_cpu1_ai_guard *guard, int error)
{
    int previous = tdvp_cpu1_ai_guard_status(guard);
    if (error >= 0) error = -EIO;
    for (;;) {
        if (previous < 0) return previous; /* First error is never overwritten. */
        if (previous == TDVP_AI_DONE) return 0; /* A stale supervisor check cannot revoke completion. */
        if (__atomic_compare_exchange_n(&guard->status, &previous, error, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return error;
    }
}

int tdvp_cpu1_ai_guard_begin(struct tdvp_cpu1_ai_guard *guard, uint64_t now, unsigned int budget_ms)
{
    int status = tdvp_cpu1_ai_guard_status(guard);
    if (status < 0) return status;
    if (status == TDVP_AI_ACTIVE) return -EBUSY;
    if (!now || !budget_ms || budget_ms > 60000U || now > UINT64_MAX - budget_ms)
        return -EINVAL;
    guard->started_ms = now;
    guard->deadline_ms = now + budget_ms;
    __atomic_store_n(&guard->status, TDVP_AI_ACTIVE, __ATOMIC_RELEASE);
    return 0;
}

int tdvp_cpu1_ai_guard_check(struct tdvp_cpu1_ai_guard *guard, const struct tdvp_cpu1_ai_guard_ops *ops)
{
    uint64_t now;
    int status = tdvp_cpu1_ai_guard_status(guard);
    if (status < 0) return status;
    if (status != TDVP_AI_ACTIVE) return -EINVAL;
    if (!ops || !ops->now || !ops->ready || ops->now(ops->context, &now) || now < guard->started_ms)
        return tdvp_cpu1_ai_guard_fail(guard, -EIO);
    if (now >= guard->deadline_ms) return tdvp_cpu1_ai_guard_fail(guard, -ETIMEDOUT);
    status = ops->ready(ops->context);
    if (status) return tdvp_cpu1_ai_guard_fail(guard, status);
    uint64_t after;
    if (ops->now(ops->context, &after) || after < now)
        return tdvp_cpu1_ai_guard_fail(guard, -EIO);
    if (after >= guard->deadline_ms) return tdvp_cpu1_ai_guard_fail(guard, -ETIMEDOUT);
    status = tdvp_cpu1_ai_guard_status(guard);
    return status == TDVP_AI_ACTIVE ? 0 : status < 0 ? status : -EINVAL;
}

int tdvp_cpu1_ai_guard_finish(struct tdvp_cpu1_ai_guard *guard, const struct tdvp_cpu1_ai_guard_ops *ops)
{
    int result = tdvp_cpu1_ai_guard_check(guard, ops), active = TDVP_AI_ACTIVE;
    if (result) return result;
    if (!__atomic_compare_exchange_n(&guard->status, &active, TDVP_AI_DONE, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return active < 0 ? active : -EINVAL;
    return 0;
}

int tdvp_cpu1_ai_guard_wait(struct tdvp_cpu1_ai_guard *guard, const struct tdvp_cpu1_ai_guard_ops *ops,
                          int expected_fd, struct pollfd *fds, nfds_t count)
{
    int result;
    if (!ops || !ops->park || !ops->poll || expected_fd < 0 || !fds || count != 1 ||
        fds[0].fd != expected_fd || fds[0].events != POLLIN) {
        result = tdvp_cpu1_ai_guard_fail(guard, -EINVAL);
        goto parked;
    }
    for (;;) {
        uint64_t now;
        result = tdvp_cpu1_ai_guard_check(guard, ops);
        if (result) goto parked;
        if (ops->now(ops->context, &now) || now < guard->started_ms) {
            result = tdvp_cpu1_ai_guard_fail(guard, -EIO); goto parked;
        }
        if (now >= guard->deadline_ms) {
            result = tdvp_cpu1_ai_guard_fail(guard, -ETIMEDOUT); goto parked;
        }
        uint64_t remaining = guard->deadline_ms - now;
        int timeout = remaining > 50 ? 50 : (int)remaining;
        fds[0].revents = 0;
        result = ops->poll(ops->context, fds, count, timeout);
        int saved_errno = errno;
        int checked = tdvp_cpu1_ai_guard_check(guard, ops);
        if (checked) { result = checked; goto parked; }
        if (result < 0 && saved_errno == EINTR) continue;
        if (!result) continue;
        if (result == 1 && (fds[0].revents == POLLIN ||
                           fds[0].revents == TDVP_AI_RTSMART_POLLIN)) return 1;
        result = tdvp_cpu1_ai_guard_fail(guard, result < 0 && saved_errno ? -saved_errno : -EIO);
        goto parked;
    }
parked:
    (void)tdvp_cpu1_ai_guard_fail(guard, result);
    /* Keep the execution stack and in-flight buffers alive. In particular,
     * do not return -1/0: pinned AI2D ignores poll's return value. */
    for (;;) {
        if (ops && ops->park) ops->park(ops->context);
        else {
            const struct timespec interval = {0, 20000000};
            (void)nanosleep(&interval, 0);
        }
    }
}
