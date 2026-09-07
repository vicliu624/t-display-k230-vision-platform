/* SPDX-License-Identifier: MIT */
#include <rtthread.h>

extern int tdvp_cpu1_vision_ownership_status(void);
extern int tdvp_cpu1_ai_clock_prepare(void);
extern int gnne_device_init(void);
extern int ai_2d_device_init(void);
extern int tdvp_cpu1_fft_init(void);

static int attempted;
static volatile int status = -RT_EBUSY;

int tdvp_cpu1_ai_status(void) { return status; }
void tdvp_cpu1_ai_fail(void) { status = -RT_EIO; }

int tdvp_cpu1_ai_init(void)
{
    const char *stage = "clock-reset";
    int result;

    if (attempted) return status;
    result = tdvp_cpu1_vision_ownership_status();
    if (result) return result;
    attempted = 1;
    if ((result = tdvp_cpu1_ai_clock_prepare())) goto failed;
    stage = "gnne";
    if ((result = gnne_device_init())) goto failed;
    stage = "ai2d";
    if ((result = ai_2d_device_init())) goto failed;
    stage = "fft";
    if ((result = tdvp_cpu1_fft_init())) goto failed;
    status = 0;
    rt_kprintf("TDVP CPU1: GNNE/AI2D/FFT registered; no model acceptance implied\n");
    return 0;
failed:
    status = result;
    rt_kprintf("TDVP CPU1: AI %s initialization failed: %d; no retry/reset\n", stage, result);
    return result;
}
