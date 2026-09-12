/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rtthread.h>
#include "sysctl_media_clk.h"
#include "sysctl_boot.h"
#include "sysctl_pwr.h"

/* Test against actual pinned layout declarations, not copied struct fixtures. */
_Static_assert(offsetof(sysctl_media_clk_t, isp_clken_cfg) == 0x64, "ISP gate layout");
_Static_assert(offsetof(sysctl_media_clk_t, isp_clkdiv_cfg) == 0x68, "ISP divider layout");
_Static_assert(offsetof(sysctl_media_clk_t, mclk_cfg) == 0x6c, "MCLK layout");
_Static_assert(offsetof(sysctl_boot_t, pll[2]) == 0x20, "PLL layout");
_Static_assert(offsetof(sysctl_pwr_s, disp_lpi_state) == 0x40, "DISP state layout");
_Static_assert(offsetof(sysctl_pwr_s, isp_pwr_lpi_state) == 0x14c, "ISP state layout");

static uint32_t cmu[64], initial[64], pll[12], power[84];
static unsigned int maps, unmaps, writes, pll_reads;
static int scenario;
extern int tdvp_cpu1_camera_clock_prepare(void);

void *rt_ioremap(void *address, unsigned long size)
{
    ++maps;
    if (scenario >= 1 && scenario <= 3 && maps == (unsigned)scenario) return NULL;
    switch ((uintptr_t)address) {
    case 0x91100000: assert(size == 0x70); return cmu;
    case 0x91102000: assert(size == 0x30); return pll;
    case 0x91103000: assert(size == 0x150); return power;
    default: abort();
    }
}
void rt_iounmap(void *address)
{
    assert(address == cmu || address == pll || address == power);
    ++unmaps;
}
uint32_t readl(const volatile void *address)
{
    uintptr_t ptr = (uintptr_t)address;
    if (ptr >= (uintptr_t)pll && ptr < (uintptr_t)(pll + 12)) {
        ++pll_reads;
        return *(const volatile uint32_t *)address ^
               (scenario == 18 && pll_reads > 12 && address == pll ? 1U : 0U);
    }
    assert((ptr >= (uintptr_t)cmu && ptr < (uintptr_t)(cmu + 28)) ||
           address == power + 0x40 / 4 || address == power + 0x14c / 4);
    if (scenario == 19 && writes && address == power + 0x40 / 4) return 1;
    return *(const volatile uint32_t *)address;
}
void writel(uint32_t value, volatile void *address)
{
    ++writes;
    /* Any PLL, DDR, power, I2C, VGLite/VO or other register write aborts. */
    if (address == cmu + 25) {
        assert((value & ~0x27101U) == (initial[25] & ~0x27101U));
        if ((scenario == 12 && !(value & 0x27101U)) ||
            (scenario == 16 && (value & 0x27101U))) return;
    } else if (address == cmu + 26) {
        assert(!(cmu[25] & 0x27101U));
        assert((value & ~0xbffe001fU) == (initial[26] & ~0xbffe001fU));
        assert(value & (1U << 31));
        if (scenario == 13) return;
    } else if (address == cmu + 27) {
        assert((value & ~0x8001fc02U) == (initial[27] & ~0x8001fc02U));
        if ((value & 0x1fc00U) != (cmu[27] & 0x1fc00U)) {
            assert(!(cmu[27] & 2U) && (value & (1U << 31)));
            if (scenario == 15) return;
        }
        if (scenario == 14 && !(value & 2U)) return;
        if (scenario == 17 && (value & 2U)) return;
    } else {
        abort();
    }
    *(volatile uint32_t *)address = value & ~(1U << 31);
}
int rt_kprintf(const char *format, ...) { (void)format; return 0; }

int main(int argc, char **argv)
{
    assert(argc == 2);
    scenario = atoi(argv[1]);
    assert(scenario >= 0 && scenario <= 21);
    for (unsigned int i = 0; i < 64; ++i) cmu[i] = 0x256789abU ^ (i * 1007U);
    cmu[24] |= 1U << 4;
    cmu[25] |= 0x27101U;
    cmu[26] = 0x128a5837U; /* incorrect camera rates, nonzero CSI0/1 fields */
    cmu[27] = 0x003a770aU; /* MCLK1 on at the wrong divider */
    pll[0] = 0x0102018fU; pll[1] = 0x200c7; pll[2] = 0x40034; pll[3] = 0x21;
    pll[4] = 0x62; pll[5] = 0x20031; pll[6] = 0x40034; pll[7] = 0x21;
    pll[8] = 0x6e; pll[9] = 0x2001b; pll[10] = 0x60035; pll[11] = 0x21;
    power[16] = 0x2a; power[83] = 0xa;
    if (scenario == 4) power[16] = 1;
    if (scenario == 5) power[83] = 1;
    if (scenario == 6) cmu[24] &= ~(1U << 4);
    if (scenario == 7) pll[0]++;
    if (scenario == 8) pll[5] |= 1U << 19;
    if (scenario == 9) pll[10] &= ~4U;
    if (scenario == 10) pll[7] &= ~1U;
    if (scenario == 11) pll[8]++;
    if (scenario == 16) cmu[25] &= ~0x27101U;
    if (scenario == 20) {
        cmu[26] = (cmu[26] & ~0x3ffe001fU) | 0x820015U;
        cmu[27] = (cmu[27] & ~0x1fc00U) | 0x18400U;
    }
    if (scenario == 21) {
        /* Read-only board snapshot, 2026-09-07. Not a hardware boot test. */
        cmu[24] = 0x43fe; cmu[25] = 0xfffff;
        cmu[26] = 0x820835; cmu[27] = 0x3a7708;
    }
    memcpy(initial, cmu, sizeof(cmu));
    int expected = 0;
    if (scenario >= 1 && scenario <= 3) expected = -RT_ENOMEM;
    if (scenario >= 4 && scenario <= 6) expected = -RT_EBUSY;
    if (scenario >= 7 && scenario <= 11) expected = -RT_EINVAL;
    if (scenario >= 12 && scenario <= 19) expected = -RT_EIO;
    assert(tdvp_cpu1_camera_clock_prepare() == expected);
    assert(unmaps == (scenario >= 1 && scenario <= 3 ? (unsigned)scenario - 1 : 3U));
    if (scenario >= 1 && scenario <= 11) assert(!writes);
    for (unsigned int i = 0; i < 64; ++i)
        if (i < 25 || i > 27) assert(cmu[i] == initial[i]);
    if (!expected) {
        assert((cmu[25] & 0x27101U) == 0x27101U);
        assert((cmu[26] & 0x3ffe001fU) == 0x820015U);
        assert((cmu[27] & 0x1fc02U) == 0x18402U);
    }
    if (scenario == 20) assert(!writes);
    printf("CPU1 camera clock: PASS case %d status %d\n", scenario, expected);
    return 0;
}
