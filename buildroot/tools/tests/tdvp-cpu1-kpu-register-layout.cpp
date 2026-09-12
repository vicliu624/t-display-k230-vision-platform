// SPDX-License-Identifier: MIT
// Host-only layout checks against the actual pinned SDK header, no MMIO.
#include "tdvp_cpu1_kpu_guard.h"
#include <nncase/runtime/k230/gnne.h>
#include <cstddef>
#include <cstdio>
static_assert(sizeof(gnne_status) == 16 && offsetof(gnne_reg_file_t, status) == 64 &&
    offsetof(gnne_reg_file_t, ctrl) == 48, "GNNE register offsets drifted");
static_assert(GNNE_STATUS_IDLE == 0 && GNNE_RESET_STATUS_NORMAL == 0 &&
    GNNE_EXCEPTION_OK == 0 && uint64_t(GNNE_CTRL_ENABLE_CLEAR) == (UINT64_C(1) << 32),
    "GNNE control encoding drifted");
int main()
{
    gnne_status status{};
    status.bits.kpu_work_status = 3;
    if (status.data[0] != TDVP_KPU_WORK_MASK || status.data[1]) return 1;
    status = {}; status.bits.reset_status = 3;
    if (status.data[0] != TDVP_KPU_RESET_MASK || status.data[1]) return 1;
    status = {}; status.bits.exception_status = 3;
    if (status.data[0] != TDVP_KPU_EXCEPTION_MASK || status.data[1]) return 1;
    status = {}; status.bits.axi_bresp_error = 1;
    if (status.data[0] != TDVP_KPU_AXI_ERROR || status.data[1]) return 1;
    std::puts("PASS actual SDK GNNE control/status layouts; no register access");
}
