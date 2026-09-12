/* SPDX-License-Identifier: MIT */
#include <rtthread.h>
#include "tdvp_cpu1_vision_layout.h"
#include "tdvp_startup_trace.h"

#if !defined(CONFIG_MEM_MMZ_BASE) || !defined(CONFIG_MEM_MMZ_SIZE)
#error "CPU1 vision requires an explicit, Linux-reserved MMZ"
#endif
_Static_assert(CONFIG_MEM_MMZ_BASE == TDVP_VISION_MMZ_BASE,
               "CPU1 MMZ base differs from the vision ownership contract");
_Static_assert(CONFIG_MEM_MMZ_SIZE == TDVP_VISION_MMZ_SIZE,
               "CPU1 MMZ size differs from the vision ownership contract");
_Static_assert(TDVP_VISION_MMZ_BASE + TDVP_VISION_MMZ_SIZE == TDVP_VISION_SHARED_BASE,
               "MMZ overlaps the Linux-visible frame transport");

/* Strong references are intentional: missing media libraries must fail the
 * link, not silently become the vendor's weak 'no library' implementations.
 * Do not initialise VO/DSI, audio, VPU, global PM or default media clocks.
 */
extern int cmpi_init(void);
extern int log_init(void);
extern int mmz_init(unsigned long base, unsigned long size);
extern int mmz_userdev_init(void);
extern int sysctrl_init(void);
extern int vb_init(void);
extern int vicap_init(void);
extern int tdvp_cpu1_vision_pins_init(void);
extern int tdvp_cpu1_i2c4_init_status(void);
extern int tdvp_cpu1_camera_clock_prepare(void);
extern int tdvp_cpu1_vision_ownership_status(void);

static int vision_status = -RT_EBUSY;
static int attempted;

int tdvp_cpu1_vision_init_status(void)
{
    return vision_status;
}

/* Called explicitly after the Linux ownership grant, never at COMPONENT init. Failed partial
 * initialization is latched; the service must report failure, never retry
 * by re-registering already initialized media devices in place.
 */
int mpp_init(void)
{
    const char *stage = "early-i2c4";
    int result;

    if (attempted)
        return vision_status;
    result = tdvp_cpu1_vision_ownership_status();
    if (result)
        return result; /* Premature callers cannot touch hardware or consume the attempt. */
    attempted = 1;
    if ((result = tdvp_cpu1_i2c4_init_status()) != 0)
        goto failed;
    stage = "camera-clocks";
    tdvp_cpu1_startup_trace(TDVP_STARTUP_CAMERA_CLOCKS, TDVP_STARTUP_PENDING);
    if ((result = tdvp_cpu1_camera_clock_prepare()) != 0)
        goto failed;
    stage = "cmpi";
    tdvp_cpu1_startup_trace(TDVP_STARTUP_CMPI, TDVP_STARTUP_PENDING);
    if ((result = cmpi_init()) != 0)
        goto failed;
    stage = "log";
    tdvp_cpu1_startup_trace(TDVP_STARTUP_LOG, TDVP_STARTUP_PENDING);
    if ((result = log_init()) != 0)
        goto failed;
    stage = "mmz";
    tdvp_cpu1_startup_trace(TDVP_STARTUP_MMZ, TDVP_STARTUP_PENDING);
    /* The vendor MMZ allocator reserves its final page. */
    if ((result = mmz_init(TDVP_VISION_MMZ_BASE, TDVP_VISION_MMZ_SIZE - 4096UL)) != 0)
        goto failed;
    stage = "mmz-userdev";
    tdvp_cpu1_startup_trace(TDVP_STARTUP_MMZ_USERDEV, TDVP_STARTUP_PENDING);
    if ((result = mmz_userdev_init()) != 0)
        goto failed;
    stage = "sysctrl";
    tdvp_cpu1_startup_trace(TDVP_STARTUP_SYSCTRL, TDVP_STARTUP_PENDING);
    if ((result = sysctrl_init()) != 0)
        goto failed;
    stage = "vb";
    tdvp_cpu1_startup_trace(TDVP_STARTUP_VB, TDVP_STARTUP_PENDING);
    if ((result = vb_init()) != 0)
        goto failed;
    stage = "camera-pins";
    tdvp_cpu1_startup_trace(TDVP_STARTUP_CAMERA_PINS, TDVP_STARTUP_PENDING);
    if ((result = tdvp_cpu1_vision_pins_init()) != 0)
        goto failed;
    stage = "vicap";
    tdvp_cpu1_startup_trace(TDVP_STARTUP_VICAP, TDVP_STARTUP_PENDING);
    if ((result = vicap_init()) != 0)
        goto failed;
    vision_status = 0;
    tdvp_cpu1_startup_trace(TDVP_STARTUP_VICAP, 0);
    rt_kprintf("TDVP CPU1 vision: media drivers initialized; capture not started\n");
    return 0;

failed:
    tdvp_cpu1_startup_trace(TDVP_STARTUP_NONE, result);
    vision_status = result;
    rt_kprintf("TDVP CPU1 vision: %s initialization failed: %d\n", stage, result);
    return result;
}
