/* SPDX-License-Identifier: MIT */
#include <rtthread.h>
#include <ioremap.h>
#include <encoding.h>
#include <tick.h>
#include "tdvp_vision_owner_io.h"
#include "tdvp_startup_trace.h"

#ifndef RT_USING_TDVP_CPU1_VISION
#error "Ownership startup must not be linked into an unpaired firmware"
#endif
extern int tdvp_cpu1_i2c4_board_init(void);
extern int mpp_init(void);
extern int tdvp_cpu1_ai_init(void);
extern int tdvp_cpu1_ai_status(void);
extern int tdvp_cpu1_vision_launch(void);

static volatile struct tdvp_owner_control *wire;
static struct tdvp_owner_session owner;
static int startup_attempted;
static volatile tdvp_v_u64 last_publication_ms;

void tdvp_cpu1_startup_trace(unsigned int stage, int result)
{
    if (!wire || (owner.own.state != TDVP_OWNER_STARTING && owner.own.state != TDVP_OWNER_READY))
        return;
    if (stage > TDVP_STARTUP_COMPLETE) return;
    if (stage) owner.own.reserved[1] = stage;
    owner.own.reserved[0] = TDVP_STARTUP_TRACE_MAGIC;
    owner.own.reserved[2] = (tdvp_v_u32)result;
    tdvp_owner_publish(&wire->cpu1_side, &owner.own);
    /* Do not increment heartbeat, refresh freshness or poll/regrant here. */
}

static tdvp_v_u64 owner_now(void)
{
    return (tdvp_v_u64)rdtime() / (TIMER_CLK_FREQ / 1000U);
}

static int owner_poll(void)
{
    struct tdvp_owner_record peer;
    int stable = tdvp_owner_snapshot(&wire->linux_side, &peer);
    int result = tdvp_owner_cpu1_step(&owner, stable ? &peer : RT_NULL, owner_now());
    if (owner.own.state == TDVP_OWNER_READY && tdvp_cpu1_ai_status()) {
        tdvp_owner_fail(&owner, TDVP_OWNER_ERR_INIT);
        result = -1;
    }
    if (owner.publish) {
        tdvp_owner_publish(&wire->cpu1_side, &owner.own);
        last_publication_ms = owner_now();
    }
    return result;
}

/* Runtime callers must not mutate the main thread's owner state machine.
 * Read its sequence-checked publication and local freshness timestamp only.
 */
int tdvp_cpu1_vision_runtime_status(void)
{
    struct tdvp_owner_record snapshot;
    /* Read the publication timestamp before the clock: an intervening owner
     * publication must not manufacture a timestamp apparently in the future.
     */
    tdvp_v_u64 published = last_publication_ms, now = owner_now();
    if (!wire || !published || now < published || now - published >= TDVP_OWNER_PEER_MS)
        return -RT_EIO;
    if (!tdvp_owner_snapshot(&wire->cpu1_side, &snapshot)) return -RT_EBUSY;
    if (snapshot.magic != TDVP_OWNER_MAGIC || snapshot.version != TDVP_OWNER_VERSION ||
        snapshot.bytes != sizeof(snapshot) || snapshot.contract != TDVP_OWNER_CONTRACT ||
        !snapshot.cookie || !snapshot.peer_cookie || !snapshot.heartbeat) return -RT_EIO;
    return snapshot.state == TDVP_OWNER_READY && !snapshot.fault ? 0 : -RT_EIO;
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
            tdvp_cpu1_startup_trace(TDVP_STARTUP_GRANT, TDVP_STARTUP_PENDING);
            result = tdvp_cpu1_i2c4_board_init();
            if (!result) result = mpp_init();
            if (!result) result = tdvp_cpu1_ai_init();
            /* A long/failed MPP call is not an excuse to use an expired grant. */
            if (!result) result = tdvp_cpu1_vision_ownership_status();
            if (!result) result = tdvp_owner_cpu1_ready(&owner);
            if (!result) {
                tdvp_owner_publish(&wire->cpu1_side, &owner.own);
                tdvp_cpu1_startup_trace(TDVP_STARTUP_LAUNCH, TDVP_STARTUP_PENDING);
                result = tdvp_cpu1_vision_launch();
                tdvp_cpu1_startup_trace(result ? TDVP_STARTUP_LAUNCH : TDVP_STARTUP_COMPLETE, result);
            }
            if (result) {
                tdvp_cpu1_startup_trace(TDVP_STARTUP_NONE, result);
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
