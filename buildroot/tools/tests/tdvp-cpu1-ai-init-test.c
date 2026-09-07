/* SPDX-License-Identifier: MIT */
#include "tdvp-cpu1-ai-mock.h"
#include "tdvp-startup-trace-stub.h"
extern int tdvp_cpu1_ai_init(void), tdvp_cpu1_ai_status(void);
extern void tdvp_cpu1_ai_fail(void);
static int granted, calls, fail_stage;
int tdvp_cpu1_vision_ownership_status(void) { return granted ? 0 : -RT_EBUSY; }
int tdvp_cpu1_ai_clock_prepare(void) { assert(++calls==1); return fail_stage==1 ? -41 : 0; }
int gnne_device_init(void) { assert(++calls==2); return fail_stage==2 ? -42 : 0; }
int ai_2d_device_init(void) { assert(++calls==3); return fail_stage==3 ? -43 : 0; }
int tdvp_cpu1_fft_init(void) { assert(++calls==4); return fail_stage==4 ? -44 : 0; }
int rt_kprintf(const char *fmt, ...) { (void)fmt; return 0; }
int main(int argc,char **argv)
{
    assert(argc==2); fail_stage=atoi(argv[1]);
    assert(tdvp_cpu1_ai_init()==-RT_EBUSY && !calls);
    assert(!trace_calls);
    granted=1;
    int result=tdvp_cpu1_ai_init();
    assert(result==(fail_stage ? -40-fail_stage : 0));
    assert(calls==(fail_stage ? fail_stage : 4));
    assert(trace_result == result);
    assert(trace_stage == TDVP_STARTUP_AI_CLOCKS + (unsigned int)(fail_stage ? fail_stage - 1 : 3));
    unsigned int saved_trace_calls = trace_calls;
    assert(tdvp_cpu1_ai_init()==result && tdvp_cpu1_ai_status()==result);
    tdvp_cpu1_ai_fail(); assert(tdvp_cpu1_ai_status()==-RT_EIO && tdvp_cpu1_ai_init()==-RT_EIO);
    assert(trace_calls == saved_trace_calls);
    puts("CPU1 AI init: PASS ordered strong dependencies, grant prerequisite, latched failures/no reinit");
}
