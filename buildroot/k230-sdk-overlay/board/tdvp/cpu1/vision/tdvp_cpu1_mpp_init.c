/* SPDX-License-Identifier: MIT */
#include <rtthread.h>
#include "tdvp_cpu1_vision_layout.h"

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

static int vision_status = -RT_EBUSY;
static int attempted;

int tdvp_cpu1_vision_init_status(void)
{
    return vision_status;
}

/* Called once by the pinned SDK's component initializer. Failed partial
 * initialization is latched; the service must report failure, never retry
 * by re-registering already initialized media devices in place.
 */
int mpp_init(void)
{
    const char *stage = "cmpi";
    int result;

    if (attempted)
        return vision_status;
    attempted = 1;
    if ((result = cmpi_init()) != 0)
        goto failed;
    stage = "log";
    if ((result = log_init()) != 0)
        goto failed;
    stage = "mmz";
    /* The vendor MMZ allocator reserves its final page. */
    if ((result = mmz_init(TDVP_VISION_MMZ_BASE, TDVP_VISION_MMZ_SIZE - 4096UL)) != 0)
        goto failed;
    stage = "mmz-userdev";
    if ((result = mmz_userdev_init()) != 0)
        goto failed;
    stage = "sysctrl";
    if ((result = sysctrl_init()) != 0)
        goto failed;
    stage = "vb";
    if ((result = vb_init()) != 0)
        goto failed;
    stage = "camera-pins";
    if ((result = tdvp_cpu1_vision_pins_init()) != 0)
        goto failed;
    stage = "vicap";
    if ((result = vicap_init()) != 0)
        goto failed;
    vision_status = 0;
    rt_kprintf("TDVP CPU1 vision: media drivers initialized; capture not started\n");
    return 0;

failed:
    vision_status = result;
    rt_kprintf("TDVP CPU1 vision: %s initialization failed: %d\n", stage, result);
    return result;
}
