/* SPDX-License-Identifier: MIT */
#include <rtthread.h>
#include <ioremap.h>
#include <encoding.h>
#include <tick.h>
#include "tdvp_vision_owner_io.h"

#ifndef RT_USING_TDVP_CPU1_VISION
#error "Ownership startup must not be linked into an unpaired firmware"
#endif
extern int tdvp_cpu1_i2c4_board_init(void);
extern int mpp_init(void);
extern int tdvp_cpu1_vision_launch(void);

static volatile struct tdvp_owner_control *wire;
static struct tdvp_owner_session owner;
static int startup_attempted;

static tdvp_v_u64 owner_now(void)
{
    return (tdvp_v_u64)rdtime() / (TIMER_CLK_FREQ / 1000U);
}

static int owner_poll(void)
{
    struct tdvp_owner_record peer;
    int stable = tdvp_owner_snapshot(&wire->linux_side, &peer);
    int result = tdvp_owner_cpu1_step(&owner, stable ? &peer : RT_NULL, owner_now());
    if (owner.publish) tdvp_owner_publish(&wire->cpu1_side, &owner.own);
    return result;
}

/* Both I2C and MPP check this before their first hardware operation. */
int tdvp_cpu1_vision_ownership_status(void)
{
    if (!wire || (owner.own.state != TDVP_OWNER_STARTING && owner.own.state != TDVP_OWNER_READY))
        return -RT_EBUSY;
    if (owner_poll() < 0) return -RT_EIO;
    return 0;
}

int tdvp_cpu1_vision_startup(void)
{
    int result, reported = 0;
    tdvp_v_u64 cookie;

    if (startup_attempted) return -RT_EBUSY;
    startup_attempted = 1;
    wire = rt_ioremap((void *)TDVP_OWNER_BASE, TDVP_OWNER_WINDOW);
    if (!wire) return -RT_ENOMEM;
    cookie = (tdvp_v_u64)rdtime();
    tdvp_owner_cpu1_init(&owner, cookie ? cookie : 1, owner_now());
    rt_kprintf("TDVP CPU1: waiting for live Linux vision ownership offer; no media access\n");
    for (;;) {
        result = owner_poll();
        if (result == 1) {
            result = tdvp_cpu1_i2c4_board_init();
            if (!result) result = mpp_init();
            /* A long/failed MPP call is not an excuse to use an expired grant. */
            if (!result) result = tdvp_cpu1_vision_ownership_status();
            if (!result) result = tdvp_owner_cpu1_ready(&owner);
            if (!result) {
                tdvp_owner_publish(&wire->cpu1_side, &owner.own);
                result = tdvp_cpu1_vision_launch();
            }
            if (result) {
                rt_kprintf("TDVP CPU1: ownership-gated initialization failed: %d\n", result);
                tdvp_owner_fail(&owner, TDVP_OWNER_ERR_INIT);
            }
        }
        if (owner.own.state == TDVP_OWNER_FAULT && !reported) {
            rt_kprintf("TDVP CPU1: ownership fault %u; no retry or DMA-buffer reclaim\n", owner.own.fault);
            reported = 1;
            if (!owner.publish) {
                /* No live offer ever observed: not even our control half was written. */
                rt_iounmap((void *)wire);
                wire = RT_NULL;
                return -RT_EIO;
            }
        }
        /* Independent of an open Linux frame reader. After a fault, retain
         * mappings/state; worker observes FAULT and stops without unsafe reset.
         * The existing ping/CRC mailbox thread remains independent throughout.
         */
        rt_thread_mdelay(50);
    }
}
