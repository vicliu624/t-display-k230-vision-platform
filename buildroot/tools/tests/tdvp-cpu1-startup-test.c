/* SPDX-License-Identifier: MIT */
/* Run the actual RT-Smart startup function with a simulated Linux peer. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rtthread.h>
#include "tdvp_vision_owner_io.h"

extern int tdvp_cpu1_vision_startup(void);
extern int tdvp_cpu1_vision_ownership_status(void);
extern int tdvp_cpu1_vision_runtime_status(void);
static struct tdvp_owner_session linux_owner;
static struct tdvp_owner_control wire;
static struct tdvp_owner_record original;
static jmp_buf done;
static uint64_t now = 1000;
static int scenario, maps, unmaps, i2c, mpp, ai, launch, ready_ticks;
int tdvp_cpu1_ai_status(void) { return scenario == 14 && ready_ticks ? -RT_EIO : 0; }
int tdvp_cpu1_ai_init(void)
{
    assert(i2c == 1 && mpp == 1 && ++ai == 1);
    if (scenario == 9) return -RT_EIO;
    assert(tdvp_cpu1_vision_ownership_status() == 0);
    return scenario == 13 ? -RT_EIO : 0;
}

uint64_t tdvp_test_rdtime(void) { return now * 27000; }
int rt_kprintf(const char *format, ...) { (void)format; return 0; }
void *rt_ioremap(void *address, unsigned long size)
{
    ++maps;
    assert((uintptr_t)address == TDVP_OWNER_BASE && size == TDVP_OWNER_WINDOW);
    return scenario == 12 ? NULL : &wire;
}
void rt_iounmap(void *address) { assert(address == &wire && !i2c && !mpp && !launch); ++unmaps; }

int tdvp_cpu1_i2c4_board_init(void)
{
    assert(tdvp_cpu1_vision_ownership_status() == 0);
    assert(wire.cpu1_side.state == TDVP_OWNER_STARTING && ++i2c == 1);
    return scenario == 6 ? -42 : 0;
}
int mpp_init(void)
{
    assert(i2c == 1 && ++mpp == 1);
    assert(tdvp_cpu1_vision_ownership_status() == 0);
    if (scenario == 9) {
        now += 61000;
        tdvp_owner_fail(&linux_owner, TDVP_OWNER_ERR_TIMEOUT);
        tdvp_owner_publish(&wire.linux_side, &linux_owner.own);
    }
    return scenario == 7 ? -43 : 0;
}
int tdvp_cpu1_vision_launch(void)
{
    assert(i2c == 1 && mpp == 1 && ai == 1 && ++launch == 1);
    assert(wire.cpu1_side.state == TDVP_OWNER_READY);
    assert(tdvp_cpu1_vision_runtime_status() == 0);
    if (scenario == 0) {
        struct tdvp_owner_record saved = wire.cpu1_side;
        wire.cpu1_side.sequence |= 1U;
        assert(tdvp_cpu1_vision_runtime_status() == -RT_EBUSY);
        wire.cpu1_side = saved;
        ++wire.cpu1_side.contract;
        assert(tdvp_cpu1_vision_runtime_status() == -RT_EIO);
        wire.cpu1_side = saved;
        now += TDVP_OWNER_PEER_MS;
        assert(tdvp_cpu1_vision_runtime_status() == -RT_EIO);
        now -= TDVP_OWNER_PEER_MS;
        assert(tdvp_cpu1_vision_runtime_status() == 0);
        assert(!memcmp(&saved, &wire.cpu1_side, sizeof(saved))); /* Read only. */
    }
    return scenario == 8 ? -44 : 0;
}

void rt_thread_mdelay(int delay)
{
    struct tdvp_owner_record peer;
    int stable;
    assert(delay == 50);
    now += delay;
    assert(now < 130000); /* No accidental infinite host-test wait. */
    if (wire.cpu1_side.state == TDVP_OWNER_FAULT) longjmp(done, 1);
    if (wire.cpu1_side.state == TDVP_OWNER_READY) {
        ++ready_ticks;
        if (scenario == 0 && ready_ticks == 10) longjmp(done, 1);
        if (scenario == 5) return; /* Linux heartbeat loss after initialization. */
        if (scenario == 10) ++linux_owner.own.cookie;
    }
    if (scenario >= 1 && scenario <= 3) return;
    stable = tdvp_owner_snapshot(&wire.cpu1_side, &peer);
    (void)tdvp_owner_linux_step(&linux_owner, stable ? &peer : NULL, now);
    if (scenario == 4) ++linux_owner.own.version;
    tdvp_owner_publish(&wire.linux_side, &linux_owner.own);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    scenario = atoi(argv[1]);
    memset(&wire, 0xa5, sizeof(wire));
    original = wire.cpu1_side;
    tdvp_owner_linux_init(&linux_owner, 123, now);
    if (scenario == 1) memset(&wire.linux_side, 0, sizeof(wire.linux_side));
    else {
        if (scenario == 3) linux_owner.own.state = TDVP_OWNER_GRANT;
        if (scenario == 4) ++linux_owner.own.version;
        tdvp_owner_publish(&wire.linux_side, &linux_owner.own);
    }
    assert(tdvp_cpu1_vision_ownership_status() == -RT_EBUSY && !maps);
    assert(tdvp_cpu1_vision_runtime_status() == -RT_EIO);
    if (!setjmp(done)) {
        int result = tdvp_cpu1_vision_startup();
        assert((scenario >= 1 && scenario <= 4) || scenario == 12);
        assert(result == (scenario == 12 ? -RT_ENOMEM : -RT_EIO));
        assert(!memcmp(&original, &wire.cpu1_side, sizeof(original)));
    }
    assert(maps == 1);
    if (scenario >= 1 && scenario <= 4) assert(unmaps == 1 && !i2c && !mpp && !launch);
    if (scenario == 12) assert(!unmaps && !i2c && !mpp && !launch);
    if (scenario == 6) assert(i2c == 1 && !mpp && !launch);
    if (scenario == 7 || scenario == 9) assert(i2c == 1 && mpp == 1 && !launch);
    if (scenario == 13) assert(ai == 1 && !launch && wire.cpu1_side.state == TDVP_OWNER_FAULT);
    if (scenario == 14) assert(ai == 1 && launch == 1 && wire.cpu1_side.state == TDVP_OWNER_FAULT);
    if (scenario == 0 || scenario == 5 || scenario == 8 || scenario == 10)
        assert(i2c == 1 && mpp == 1 && launch == 1);
    if (scenario == 0) assert(wire.cpu1_side.state == TDVP_OWNER_READY);
    if (scenario >= 5 && scenario <= 10) {
        assert(wire.cpu1_side.state == TDVP_OWNER_FAULT);
        assert(tdvp_cpu1_vision_ownership_status() != 0);
        assert(tdvp_cpu1_vision_runtime_status() != 0);
    }
    assert(tdvp_cpu1_vision_startup() == -RT_EBUSY); /* No in-place restart. */
    printf("CPU1 production startup: PASS scenario %d\n", scenario);
    return 0;
}
