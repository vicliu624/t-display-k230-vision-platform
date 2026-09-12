/* SPDX-License-Identifier: MIT */
#include <rtthread.h>
#include "tdvp_startup_trace.h"

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
    tdvp_cpu1_startup_trace(TDVP_STARTUP_AI_CLOCKS, TDVP_STARTUP_PENDING);
    if ((result = tdvp_cpu1_ai_clock_prepare())) goto failed;
    stage = "gnne";
    tdvp_cpu1_startup_trace(TDVP_STARTUP_GNNE, TDVP_STARTUP_PENDING);
    if ((result = gnne_device_init())) goto failed;
    stage = "ai2d";
    tdvp_cpu1_startup_trace(TDVP_STARTUP_AI2D, TDVP_STARTUP_PENDING);
    if ((result = ai_2d_device_init())) goto failed;
    stage = "fft";
    tdvp_cpu1_startup_trace(TDVP_STARTUP_FFT, TDVP_STARTUP_PENDING);
    if ((result = tdvp_cpu1_fft_init())) goto failed;
    status = 0;
    tdvp_cpu1_startup_trace(TDVP_STARTUP_FFT, 0);
    rt_kprintf("TDVP CPU1: GNNE/AI2D/FFT registered; no model acceptance implied\n");
    return 0;
failed:
    tdvp_cpu1_startup_trace(TDVP_STARTUP_NONE, result);
    status = result;
    rt_kprintf("TDVP CPU1: AI %s initialization failed: %d; no retry/reset\n", stage, result);
    return result;
}
