/* SPDX-License-Identifier: MIT */
/* Only for isolated init-call tests; the startup test uses the real publisher. */
#include "tdvp_startup_trace.h"
static unsigned int trace_stage, trace_calls;
static int trace_result;
void tdvp_cpu1_startup_trace(unsigned int stage, int result)
{
    assert(stage <= TDVP_STARTUP_COMPLETE);
    if (stage) trace_stage = stage;
    trace_result = result;
    ++trace_calls;
}
