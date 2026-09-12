/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include "tdvp-startup-trace-stub.h"

extern int mpp_init(void);
extern int tdvp_cpu1_vision_init_status(void);
static int fail_at;
static int calls;
static int ownership;
int tdvp_cpu1_vision_ownership_status(void) { return ownership ? 0 : -7; }

static int step(int expected)
{
    static const unsigned int stages[] = {TDVP_STARTUP_CMPI, TDVP_STARTUP_LOG,
        TDVP_STARTUP_MMZ, TDVP_STARTUP_MMZ_USERDEV, TDVP_STARTUP_SYSCTRL,
        TDVP_STARTUP_VB, TDVP_STARTUP_CAMERA_PINS, TDVP_STARTUP_VICAP};
    assert(trace_stage == stages[expected - 1] && trace_result == TDVP_STARTUP_PENDING);
    assert(++calls == expected);
    return calls == fail_at ? -100 - calls : 0;
}

int cmpi_init(void) { return step(1); }
int tdvp_cpu1_i2c4_init_status(void) { return fail_at == 9 ? -109 : 0; }
int tdvp_cpu1_camera_clock_prepare(void) { assert(fail_at != 9); return fail_at == 10 ? -110 : 0; }
int log_init(void) { return step(2); }
int mmz_init(unsigned long base, unsigned long size)
{
    assert(base == 0x14000000UL);
    assert(size == 0x08000000UL - 4096UL);
    return step(3);
}
int mmz_userdev_init(void) { return step(4); }
int sysctrl_init(void) { return step(5); }
int vb_init(void) { return step(6); }
int tdvp_cpu1_vision_pins_init(void) { return step(7); }
int vicap_init(void) { return step(8); }
int rt_kprintf(const char *format, ...) { (void)format; return 0; }

int main(int argc, char **argv)
{
    assert(argc == 2);
    fail_at = atoi(argv[1]);
    assert(fail_at >= 0 && fail_at <= 10);
    assert(tdvp_cpu1_vision_init_status() != 0);
    const int expected = fail_at ? -100 - fail_at : 0;
    assert(mpp_init() == -7 && !calls); /* No ownership: no initialization attempt. */
    assert(!trace_calls);
    ownership = 1;
    assert(mpp_init() == expected);
    assert(tdvp_cpu1_vision_init_status() == expected);
    assert(trace_result == expected);
    const unsigned int saved_trace_calls = trace_calls;
    assert(calls == (fail_at >= 9 ? 0 : (fail_at ? fail_at : 8)));
    /* Neither success nor partially initialized failure may run twice. */
    assert(mpp_init() == expected);
    assert(trace_calls == saved_trace_calls);
    assert(calls == (fail_at >= 9 ? 0 : (fail_at ? fail_at : 8)));
    return 0;
}
