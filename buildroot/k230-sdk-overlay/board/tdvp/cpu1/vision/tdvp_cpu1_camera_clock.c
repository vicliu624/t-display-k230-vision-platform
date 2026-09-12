/* SPDX-License-Identifier: MIT */
#include <rtthread.h>
#include <ioremap.h>
#include <riscv_io.h>
#include <stdint.h>

/* Paired vision firmware only, before any ISP registration or sensor access.
 * Linux owns the shared power/PLL/DDR lifecycle. Observe those resources;
 * never cycle DISP (also drives the Linux screen) or retune a shared PLL.
 * The ownership DT must disable every Linux writer of 0x64/0x68/0x6c.
 */
#define ISP_GATE (0x64 / 4)
#define ISP_DIV (0x68 / 4)
#define MCLK (0x6c / 4)
#define ISP_GATES (1U | (1U << 8) | (7U << 12) | (1U << 17))
#define ISP_RATE_MASK (31U | (63U << 17) | (127U << 23))
/* PLL1/4/22 = 27 MHz CFG; PLL2/4/2 = 333..333.375 MHz CSI2;
 * PLL0/4/1 = 400 MHz ISP; PLL0/4/2 = 200 MHz HCLK.
 */
#define ISP_RATE (21U | (1U << 17) | (1U << 23))
#define MCLK_GATE (1U << 1)
#define MCLK_RATE_MASK (127U << 10)
/* Pinned GC2093 CSI2 1080p30 table: PLL1/4/25 = 23.76 MHz, MCLK1. */
#define MCLK_RATE ((1U << 10) | (24U << 12))
#define WRITE_ENABLE (1U << 31)

int tdvp_cpu1_camera_clock_prepare(void)
{
    volatile uint32_t *cmu = RT_NULL, *pll = RT_NULL, *power = RT_NULL;
    uint32_t cfg[3][3], gate, value;
    unsigned int i;
    int result = -RT_ENOMEM;

    cmu = rt_ioremap((void *)0x91100000UL, 0x70);
    if (!cmu) goto out;
    pll = rt_ioremap((void *)0x91102000UL, 0x30);
    if (!pll) goto out;
    power = rt_ioremap((void *)0x91103000UL, 0x150);
    if (!power) goto out;

    result = -RT_EBUSY;
    /* A Linux ownership-ready handshake must precede production activation.
     * This guard is not that handshake: an already-on domain alone cannot
     * prove Linux relinquished the camera. No power writes on failure.
     */
    if ((readl(power + 0x40 / 4) & 3U) != 2U ||
        (readl(power + 0x14c / 4) & 3U) != 2U ||
        !(readl(cmu + 0x60 / 4) & (1U << 4))) goto out;
    result = -RT_EINVAL;
    for (i = 0; i < 3; ++i) {
        uint64_t numerator, denominator;
        cfg[i][0] = readl(pll + i * 4);
        cfg[i][1] = readl(pll + i * 4 + 1);
        cfg[i][2] = readl(pll + i * 4 + 2);
        if ((cfg[i][1] & (1U << 19)) || !(cfg[i][2] & 4U) ||
            !(readl(pll + i * 4 + 3) & 1U)) goto out;
        numerator = 24000000ULL * ((cfg[i][0] & 0x1fffU) + 1U);
        denominator = (((cfg[i][0] >> 16) & 63U) + 1ULL) *
                      (((cfg[i][0] >> 24) & 15U) + 1ULL);
        if ((i == 0 && numerator != 1600000000ULL * denominator) ||
            (i == 1 && numerator != 2376000000ULL * denominator) ||
            (i == 2 && (numerator < 2664000000ULL * denominator ||
                        numerator > 2667000000ULL * denominator))) goto out;
    }

    result = -RT_EIO;
    value = readl(cmu + ISP_DIV);
    if ((value & ISP_RATE_MASK) != ISP_RATE) {
        gate = readl(cmu + ISP_GATE);
        if (gate & ISP_GATES) {
            writel(gate & ~ISP_GATES, cmu + ISP_GATE);
            if (readl(cmu + ISP_GATE) & ISP_GATES) goto out;
        }
        writel((value & ~ISP_RATE_MASK) | ISP_RATE | WRITE_ENABLE, cmu + ISP_DIV);
        if ((readl(cmu + ISP_DIV) & ISP_RATE_MASK) != ISP_RATE) goto out;
    }
    value = readl(cmu + MCLK);
    if ((value & MCLK_RATE_MASK) != MCLK_RATE) {
        if (value & MCLK_GATE) {
            writel(value & ~MCLK_GATE, cmu + MCLK);
            if (readl(cmu + MCLK) & MCLK_GATE) goto out;
        }
        value = readl(cmu + MCLK);
        writel((value & ~MCLK_RATE_MASK) | MCLK_RATE | WRITE_ENABLE, cmu + MCLK);
        if ((readl(cmu + MCLK) & MCLK_RATE_MASK) != MCLK_RATE) goto out;
    }
    gate = readl(cmu + ISP_GATE);
    if ((gate & ISP_GATES) != ISP_GATES) writel(gate | ISP_GATES, cmu + ISP_GATE);
    value = readl(cmu + MCLK);
    if (!(value & MCLK_GATE)) writel(value | MCLK_GATE, cmu + MCLK);
    if ((readl(cmu + ISP_GATE) & ISP_GATES) != ISP_GATES ||
        !(readl(cmu + MCLK) & MCLK_GATE)) goto out;
    __sync_synchronize();
    /* Refuse to register drivers if a shared prerequisite changed mid-setup. */
    for (i = 0; i < 3; ++i)
        if (readl(pll + i * 4) != cfg[i][0] || readl(pll + i * 4 + 1) != cfg[i][1] ||
            !(readl(pll + i * 4 + 2) & 4U) || !(readl(pll + i * 4 + 3) & 1U)) goto out;
    if ((readl(power + 0x40 / 4) & 3U) != 2U ||
        (readl(power + 0x14c / 4) & 3U) != 2U ||
        !(readl(cmu + 0x60 / 4) & (1U << 4))) goto out;
    result = 0;
out:
    if (power) rt_iounmap((void *)power);
    if (pll) rt_iounmap((void *)pll);
    if (cmu) rt_iounmap((void *)cmu);
    /* MPP latches errors. Do not restore old clocks after partial failure:
     * restoration can hide the fault or interfere with another bus master.
     */
    if (result) rt_kprintf("TDVP CPU1: camera clock prerequisites refused: %d\n", result);
    return result;
}
