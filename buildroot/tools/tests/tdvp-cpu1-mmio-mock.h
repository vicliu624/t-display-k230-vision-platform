/* SPDX-License-Identifier: MIT */
#ifndef TDVP_CPU1_MMIO_MOCK_H
#define TDVP_CPU1_MMIO_MOCK_H
#include "tdvp-cpu1-ai-mock.h"
typedef long rt_base_t;
typedef uintptr_t rt_size_t;
rt_base_t rt_hw_interrupt_disable(void);
void rt_hw_interrupt_enable(rt_base_t);
uint64_t tdvp_mmu_test_rdtime(void);
#define rdtime() tdvp_mmu_test_rdtime()
#endif
