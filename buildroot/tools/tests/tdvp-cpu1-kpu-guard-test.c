/* SPDX-License-Identifier: MIT */
#include "tdvp_cpu1_kpu_guard.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
struct fake {
    uint64_t now, initial, final;
    unsigned int reads, writes, parks, settling, step, status_delay;
    int clock_error, owner_error, read_error, write_error;
    int lose_owner_on_read, lose_owner_on_write, backwards, clock_freeze;
};
static struct fake fake;
static struct tdvp_cpu1_ai_guard guard;
static int clock_now(void *context, uint64_t *now)
{
    struct fake *f = context; *now = f->now;
    return f->clock_error;
}
static int owner_ready(void *context) { return ((struct fake *)context)->owner_error; }
static void delay(void *context)
{
    struct fake *f = context; ++f->parks;
    if (f->backwards) --f->now;
    else if (!f->clock_freeze) f->now += f->step;
}
static int status_read(void *context, uint64_t *status)
{
    struct fake *f = context; ++f->reads; f->now += f->status_delay;
    if (f->lose_owner_on_read) f->owner_error = -EPIPE;
    if (f->read_error) return f->read_error;
    if (!f->writes) *status = f->initial;
    else if (f->settling) { --f->settling; *status = UINT64_C(1) << 24; }
    else *status = f->final;
    return 0;
}
static int disable(void *context)
{
    struct fake *f = context; ++f->writes;
    if (f->lose_owner_on_write) f->owner_error = -EPIPE;
    return f->write_error;
}
static const struct tdvp_cpu1_ai_guard_ops lifetime = {clock_now, owner_ready, NULL, delay, &fake};
static const struct tdvp_cpu1_kpu_init_ops hardware = {status_read, disable, &fake};
static unsigned int cases;
static void reset(void)
{
    memset(&fake, 0, sizeof(fake)); memset(&guard, 0, sizeof(guard));
    fake.now = 1000; fake.step = 10;
    REQUIRE(tdvp_cpu1_ai_guard_begin(&guard, fake.now, 5000) == 0);
}
static void expect(int result, unsigned int writes)
{
    REQUIRE(tdvp_cpu1_kpu_prepare(&guard, &lifetime, &hardware) == result);
    REQUIRE(fake.writes == writes);
    REQUIRE(fake.reads <= TDVP_KPU_INIT_MAX_READS + 1);
    if (result) {
        REQUIRE(tdvp_cpu1_ai_guard_status(&guard) == result);
        unsigned int reads = fake.reads, parks = fake.parks;
        REQUIRE(tdvp_cpu1_kpu_prepare(&guard, &lifetime, &hardware) == result);
        REQUIRE(fake.reads == reads && fake.writes == writes && fake.parks == parks);
    } else REQUIRE(tdvp_cpu1_ai_guard_status(&guard) == TDVP_AI_ACTIVE);
    ++cases;
}
int main(void)
{
    reset(); expect(0, 1); REQUIRE(fake.reads == 2 && fake.parks == 0);
    reset(); fake.settling = 3; expect(0, 1); REQUIRE(fake.parks == 3);
    for (unsigned int state = 1; state <= 3; ++state) {
        reset(); fake.initial = (uint64_t)state << 14; expect(-EBUSY, 0);
        reset(); fake.initial = (uint64_t)state << 24; expect(-EBUSY, 0);
        reset(); fake.initial = (uint64_t)state << 22; expect(-EIO, 0);
        reset(); fake.final = (uint64_t)state << 14; expect(-EBUSY, 1);
        reset(); fake.final = (uint64_t)state << 22; expect(-EIO, 1);
    }
    reset(); fake.initial = TDVP_KPU_AXI_ERROR; expect(-EIO, 0);
    reset(); fake.final = TDVP_KPU_AXI_ERROR; expect(-EIO, 1);
    reset(); fake.read_error = -ENODEV; expect(-ENODEV, 0);
    reset(); fake.write_error = -ENODEV; expect(-ENODEV, 1);
    reset(); fake.write_error = 1; expect(-EIO, 1);
    reset(); fake.clock_error = -EIO; expect(-EIO, 0);
    reset(); fake.owner_error = -EPIPE; expect(-EPIPE, 0);
    reset(); fake.lose_owner_on_read = 1; expect(-EPIPE, 0);
    reset(); fake.lose_owner_on_write = 1; expect(-EPIPE, 1);
    reset(); fake.settling = 200; expect(-ETIMEDOUT, 1); REQUIRE(fake.parks == 10);
    reset(); fake.settling = 200; fake.clock_freeze = 1; expect(-ETIMEDOUT, 1);
    REQUIRE(fake.reads == 101 && fake.parks == 100);
    reset(); fake.settling = 200; fake.backwards = 1; expect(-EIO, 1);
    reset(); fake.settling = 200; guard.deadline_ms = fake.now + 20; expect(-ETIMEDOUT, 1);
    reset(); fake.status_delay = 101; expect(-ETIMEDOUT, 0);
    reset(); fake.status_delay = 51; expect(-ETIMEDOUT, 1);
    reset(); fake.now = guard.deadline_ms; expect(-ETIMEDOUT, 0);
    reset(); fake.now = guard.started_ms - 1; expect(-EIO, 0);
    reset(); guard.status = -ECANCELED; expect(-ECANCELED, 0);
    reset(); guard.status = 0; expect(-EINVAL, 0);
    reset(); guard.status = TDVP_AI_DONE;
    REQUIRE(tdvp_cpu1_kpu_prepare(&guard, &lifetime, &hardware) == -EINVAL);
    REQUIRE(fake.reads == 0 && fake.writes == 0 && guard.status == TDVP_AI_DONE); ++cases;
    reset(); REQUIRE(tdvp_cpu1_kpu_prepare(NULL, &lifetime, &hardware) == -EINVAL); ++cases;
    reset(); REQUIRE(tdvp_cpu1_kpu_prepare(&guard, NULL, &hardware) == -EINVAL);
    REQUIRE(fake.reads == 0 && fake.writes == 0); ++cases;
    reset(); REQUIRE(tdvp_cpu1_kpu_prepare(&guard, &lifetime, NULL) == -EINVAL);
    REQUIRE(fake.reads == 0 && fake.writes == 0); ++cases;
    for (unsigned int scenario = 0; scenario < 18; ++scenario) {
        int event = 1, expected = 0;
        reset();
        switch (scenario) {
        case 0: break;
        case 1: event = 0; expected = -EINVAL; break;
        case 2: event = -1; expected = -EINVAL; break;
        case 3: event = 2; expected = -EINVAL; break;
        case 4: case 5: case 6:
            fake.initial = (uint64_t)(scenario - 3) << 14; expected = -EBUSY; break;
        case 7: case 8: case 9:
            fake.initial = (uint64_t)(scenario - 6) << 24; expected = -EBUSY; break;
        case 10: case 11: case 12:
            fake.initial = (uint64_t)(scenario - 9) << 22; expected = -EIO; break;
        case 13: fake.initial = TDVP_KPU_AXI_ERROR; expected = -EIO; break;
        case 14: fake.read_error = -ENODEV; expected = -ENODEV; break;
        case 15: fake.lose_owner_on_read = 1; expected = -EPIPE; break;
        case 16: fake.status_delay = 5000; expected = -ETIMEDOUT; break;
        case 17: fake.owner_error = -EPIPE; expected = -EPIPE; break;
        }
        REQUIRE(tdvp_cpu1_kpu_complete(&guard, &lifetime, &hardware, event) == expected);
        REQUIRE(fake.writes == 0 && fake.reads <= 1 && fake.parks == 0);
        REQUIRE(tdvp_cpu1_ai_guard_status(&guard) == (expected ? expected : TDVP_AI_ACTIVE));
        if (expected) {
            unsigned int reads = fake.reads;
            REQUIRE(tdvp_cpu1_kpu_complete(&guard, &lifetime, &hardware, 1) == expected);
            REQUIRE(fake.reads == reads && fake.writes == 0);
        }
        ++cases;
    }
    printf("PASS %u KPU initialization/completion guard cases; no hardware operations or inference acceptance\n", cases);
    return 0;
}
