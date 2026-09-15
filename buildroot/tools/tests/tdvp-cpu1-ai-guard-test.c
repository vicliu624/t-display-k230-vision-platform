/* SPDX-License-Identifier: MIT */
#include "tdvp_cpu1_ai_guard.h"
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#ifdef TDVP_TEST_PINNED_POLL
#include "tdvp-poll-conversion.h"
#endif

static struct tdvp_cpu1_ai_guard guard;
static uint64_t now_ms;
static int scenario, polls, parks, returned, released, ready_calls;
static jmp_buf parked;
static int success_case(void) { return scenario <= 2 || scenario == 16; }
static int now(void *context, uint64_t *ms)
{
    assert(context == &guard);
    if (scenario == 9 && polls) return -1;
    *ms = scenario == 10 && polls ? 1 : now_ms;
    return 0;
}
static int ready(void *context)
{
    assert(context == &guard); ++ready_calls;
    if (scenario == 11 && polls) return -ESHUTDOWN;
    if (scenario == 15) now_ms += 500; /* Ready-check work also counts. */
    return 0;
}
static int poll_one(void *context, struct pollfd *fds, nfds_t count, int timeout)
{
    assert(context == &guard && count == 1 && fds->fd == 7);
    assert(timeout > 0 && timeout <= 50 && !fds->revents);
    ++polls;
    now_ms += (unsigned int)timeout;
    if (scenario == 1 && polls < 3) return 0;
    if (scenario == 2 && polls < 3) { errno = EINTR; return -1; }
    if (scenario == 3) return 0;
    if (scenario == 4) { errno = EBADF; return -1; }
    if (scenario == 5) { fds->revents = POLLERR; return 1; }
    if (scenario == 6) { fds->revents = POLLIN | POLLHUP; return 1; }
    if (scenario == 7) { fds->revents = POLLNVAL; return 1; }
    if (scenario == 8) return 1; /* A count without an event is not completion. */
    if (scenario == 12) (void)tdvp_cpu1_ai_guard_fail(&guard, -ECANCELED);
    if (scenario == 13) now_ms = guard.deadline_ms; /* Late readiness. */
    if (scenario >= 16) {
        short event = POLLIN;
#ifdef TDVP_TEST_PINNED_POLL
        musl2dfs_events(&event);
        assert(event == 1);
        dfs2musl_events(&event);
        assert(event == TDVP_AI_RTSMART_POLLIN);
#else
        event = TDVP_AI_RTSMART_POLLIN;
#endif
        if (scenario == 17) event |= POLLOUT;
        if (scenario == 18) event |= POLLERR;
        if (scenario == 19) event |= POLLHUP;
        if (scenario == 20) event |= POLLNVAL;
        if (scenario == 21) event &= ~POLLIN;
        fds->revents = event;
        return 1;
    }
    fds->revents = POLLIN;
    return 1;
}
static void park(void *context)
{
    assert(context == &guard && !returned && !released);
    assert(tdvp_cpu1_ai_guard_status(&guard) < 0);
    /* Returning from park twice must still never return to the library. */
    if (++parks == 3) longjmp(parked, 1);
}
int main(void)
{
    struct tdvp_cpu1_ai_guard_ops ops = {now, ready, poll_one, park, &guard};
    for (scenario = 0; scenario <= 21; ++scenario) {
        memset(&guard, 0, sizeof(guard));
        now_ms = 100; polls = parks = returned = released = ready_calls = 0;
        assert(!tdvp_cpu1_ai_guard_begin(&guard, now_ms, 300));
        assert(tdvp_cpu1_ai_guard_begin(&guard, now_ms, 300) == -EBUSY);
        if (!setjmp(parked)) {
            struct pollfd fd = {.fd = scenario == 14 ? 8 : 7, .events = POLLIN};
            assert(tdvp_cpu1_ai_guard_wait(&guard, &ops, 7, &fd, 1) == 1);
            returned = 1;
            assert(success_case());
            assert(!tdvp_cpu1_ai_guard_finish(&guard, &ops));
            released = 1; /* Caller cleanup is legal only on proven success. */
        } else {
            int fault = tdvp_cpu1_ai_guard_status(&guard);
            assert(!success_case() && fault < 0 && parks == 3 && !returned && !released);
            if (scenario == 3 || scenario == 13 || scenario == 15) assert(fault == -ETIMEDOUT);
            if (scenario == 4) assert(fault == -EBADF);
            if (scenario == 11) assert(fault == -ESHUTDOWN);
            if (scenario == 12) assert(fault == -ECANCELED);
            assert(tdvp_cpu1_ai_guard_fail(&guard, -EIO) == fault);
            assert(tdvp_cpu1_ai_guard_begin(&guard, now_ms + 1, 300) == fault);
            assert(tdvp_cpu1_ai_guard_finish(&guard, &ops) == fault);
        }
    }
    memset(&guard, 0, sizeof(guard)); scenario = 0; polls = 0; now_ms = 100;
    assert(tdvp_cpu1_ai_guard_begin(&guard, 0, 1) == -EINVAL);
    assert(tdvp_cpu1_ai_guard_begin(&guard, UINT64_MAX, 2) == -EINVAL);
    assert(tdvp_cpu1_ai_guard_begin(&guard, 100, 60001) == -EINVAL);
    assert(!tdvp_cpu1_ai_guard_begin(&guard, now_ms, 300));
    assert(!tdvp_cpu1_ai_guard_finish(&guard, &ops));
    assert(!tdvp_cpu1_ai_guard_fail(&guard, -ETIMEDOUT));
    assert(tdvp_cpu1_ai_guard_status(&guard) == TDVP_AI_DONE);
    assert(!tdvp_cpu1_ai_guard_begin(&guard, now_ms, 300));
    now_ms = guard.deadline_ms;
    assert(tdvp_cpu1_ai_guard_check(&guard, &ops) == -ETIMEDOUT);
    assert(tdvp_cpu1_ai_guard_finish(&guard, &ops) == -ETIMEDOUT);
    puts("CPU1 AI guard: PASS 22 poll/lifetime cases, RT-Smart read aliases, immutable deadlines, first-error latch, late completion and no library return on fault");
}
