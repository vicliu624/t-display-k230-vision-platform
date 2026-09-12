/* SPDX-License-Identifier: MIT */
#ifndef TDVP_CPU1_KPU_GUARD_H
#define TDVP_CPU1_KPU_GUARD_H
#include "tdvp_cpu1_ai_guard.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Pinned nncase 2.9 gnne.h status register fields. These are NOT proof that
 * all DMA/bus users are quiescent, nor authorization to reclaim any buffer. */
#define TDVP_KPU_WORK_MASK (UINT64_C(3) << 14)
#define TDVP_KPU_EXCEPTION_MASK (UINT64_C(3) << 22)
#define TDVP_KPU_RESET_MASK (UINT64_C(3) << 24)
#define TDVP_KPU_AXI_ERROR (UINT64_C(1) << 26)
#define TDVP_KPU_INIT_WAIT_MS 100U
#define TDVP_KPU_INIT_MAX_READS 100U

struct tdvp_cpu1_kpu_init_ops {
    int (*status)(void *context, uint64_t *value);
    /* The sole write is the vendor's ENABLE_CLEAR, never a shared reset,
     * PLL change, retry, resume, or error recovery. Called only from IDLE. */
    int (*disable)(void *context);
    void *context;
};
int tdvp_cpu1_kpu_prepare(struct tdvp_cpu1_ai_guard *guard,
    const struct tdvp_cpu1_ai_guard_ops *lifetime,
    const struct tdvp_cpu1_kpu_init_ops *hardware);
/* Additional status check AFTER the real GNNE completion event. Not a
 * substitute for that event, model correctness, or external DMA exclusion.
 * No writes, reset, resume or polling loop on an unexpected device state. */
int tdvp_cpu1_kpu_complete(struct tdvp_cpu1_ai_guard *guard,
    const struct tdvp_cpu1_ai_guard_ops *lifetime,
    const struct tdvp_cpu1_kpu_init_ops *hardware, int event_seen);
#ifdef __cplusplus
}
#endif
#endif
