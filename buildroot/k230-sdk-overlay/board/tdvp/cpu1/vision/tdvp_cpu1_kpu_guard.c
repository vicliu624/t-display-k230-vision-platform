/* SPDX-License-Identifier: MIT */
#include "tdvp_cpu1_kpu_guard.h"
#include <errno.h>

int tdvp_cpu1_kpu_complete(struct tdvp_cpu1_ai_guard *guard,
    const struct tdvp_cpu1_ai_guard_ops *lifetime,
    const struct tdvp_cpu1_kpu_init_ops *hardware, int event_seen)
{
    uint64_t status;
    int error;
    if (!guard) return -EINVAL;
    if (!hardware || !hardware->status || event_seen != 1) { error = -EINVAL; goto failed; }
    error = tdvp_cpu1_ai_guard_check(guard, lifetime);
    if (error) goto failed;
    error = hardware->status(hardware->context, &status);
    if (error) goto failed;
    error = tdvp_cpu1_ai_guard_check(guard, lifetime);
    if (error) goto failed;
    if (status & (TDVP_KPU_EXCEPTION_MASK | TDVP_KPU_AXI_ERROR)) { error = -EIO; goto failed; }
    if (status & (TDVP_KPU_WORK_MASK | TDVP_KPU_RESET_MASK)) { error = -EBUSY; goto failed; }
    return 0;
failed:
    {
        int latched = tdvp_cpu1_ai_guard_fail(guard, error);
        return latched ? latched : error;
    }
}

int tdvp_cpu1_kpu_prepare(struct tdvp_cpu1_ai_guard *guard,
    const struct tdvp_cpu1_ai_guard_ops *lifetime,
    const struct tdvp_cpu1_kpu_init_ops *hardware)
{
    uint64_t started, previous, now, status;
    int error;
    if (!guard) return -EINVAL;
    if (!lifetime || !lifetime->now || !lifetime->park || !hardware ||
        !hardware->status || !hardware->disable) {
        error = -EINVAL; goto failed;
    }
    error = tdvp_cpu1_ai_guard_check(guard, lifetime);
    if (error) goto failed;
    if (lifetime->now(lifetime->context, &started) || !started ||
        started > UINT64_MAX - TDVP_KPU_INIT_WAIT_MS) {
        error = -EIO; goto failed;
    }
    error = hardware->status(hardware->context, &status);
    if (error) goto failed;
    if (status & (TDVP_KPU_EXCEPTION_MASK | TDVP_KPU_AXI_ERROR)) {
        error = -EIO; goto failed;
    }
    if (status & (TDVP_KPU_WORK_MASK | TDVP_KPU_RESET_MASK)) {
        error = -EBUSY; goto failed;
    }
    error = tdvp_cpu1_ai_guard_check(guard, lifetime);
    if (error) goto failed;
    if (lifetime->now(lifetime->context, &now) || now < started) {
        error = -EIO; goto failed;
    }
    if (now - started >= TDVP_KPU_INIT_WAIT_MS) {
        error = -ETIMEDOUT; goto failed;
    }
    error = hardware->disable(hardware->context);
    if (error) goto failed;
    previous = now;
    for (unsigned int read = 0; read < TDVP_KPU_INIT_MAX_READS; ++read) {
        error = tdvp_cpu1_ai_guard_check(guard, lifetime);
        if (error) goto failed;
        if (lifetime->now(lifetime->context, &now) || now < previous) {
            error = -EIO; goto failed;
        }
        if (now - started >= TDVP_KPU_INIT_WAIT_MS) {
            error = -ETIMEDOUT; goto failed;
        }
        previous = now;
        error = hardware->status(hardware->context, &status);
        if (error) goto failed;
        error = tdvp_cpu1_ai_guard_check(guard, lifetime);
        if (error) goto failed;
        if (lifetime->now(lifetime->context, &now) || now < previous) {
            error = -EIO; goto failed;
        }
        if (now - started >= TDVP_KPU_INIT_WAIT_MS) {
            error = -ETIMEDOUT; goto failed;
        }
        previous = now;
        if (status & (TDVP_KPU_EXCEPTION_MASK | TDVP_KPU_AXI_ERROR)) {
            error = -EIO; goto failed;
        }
        if (status & TDVP_KPU_WORK_MASK) { error = -EBUSY; goto failed; }
        if (!(status & TDVP_KPU_RESET_MASK)) return 0;
        lifetime->park(lifetime->context);
    }
    /* Also bound register reads if a broken clock stops advancing. */
    error = -ETIMEDOUT;
failed:
    {
        int latched = tdvp_cpu1_ai_guard_fail(guard, error);
        /* DONE ignores a stale fault publication, but must never make an
         * invalid invocation appear to have initialized the hardware. */
        return latched ? latched : error;
    }
}
