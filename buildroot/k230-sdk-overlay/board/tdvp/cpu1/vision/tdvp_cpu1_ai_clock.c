/* SPDX-License-Identifier: MIT */
#include <rtthread.h>
#include <rthw.h>
#include <ioremap.h>
#include <riscv_io.h>
#include <stdint.h>

extern int tdvp_cpu1_vision_ownership_status(void);

#define AI_CLOCK 0x08
#define AI_GATES (1U | (1U << 10))
#define AI_RATE_MASK ((1U << 2) | (7U << 3))
#define AI_RESET_DONE (1U << 31)

/* Once per granted boot, before ANY AI driver or model can run. Dedicated
 * AI clock/reset only. DDR/PLL/power are Linux-held prerequisites, never
 * written here. No generic media-frequency or shared-system reset helpers.
 */
int tdvp_cpu1_ai_clock_prepare(void)
{
    volatile uint32_t *cmu = RT_NULL, *pll = RT_NULL, *power = RT_NULL, *reset = RT_NULL;
    volatile uint32_t *reset_page = RT_NULL;
    uint32_t cfg0, cfg1, control, value;
    uint64_t numerator, denominator;
    unsigned int waited;
    int result = tdvp_cpu1_vision_ownership_status();

    if (result) return result;
    result = -RT_ENOMEM;
    cmu = rt_ioremap((void *)0x91100000UL, 0x64);
    pll = rt_ioremap((void *)0x91102000UL, 0x10);
    power = rt_ioremap((void *)0x91103000UL, 0x164);
    /* Retain an aligned mapping base for the pinned RT-Smart iounmap.
     * Passing the register pointer at +0x14 would unmap a second page.
     */
    reset_page = rt_ioremap((void *)0x91101000UL, 0x1000);
    if (!cmu || !pll || !power || !reset_page) goto out;
    reset = reset_page + 0x14 / sizeof(uint32_t);
    result = -RT_EBUSY;
    if ((readl(power + 0x2c / 4) & 3U) != 2U ||
        !(readl(power + 0x160 / 4) & 2U) ||
        (readl(cmu + 0x60 / 4) & 0x50U) != 0x50U) goto out;
    cfg0 = readl(pll); cfg1 = readl(pll + 1); control = readl(pll + 2);
    numerator = 24000000ULL * ((cfg0 & 0x1fffU) + 1U);
    denominator = (((cfg0 >> 16) & 63U) + 1ULL) * (((cfg0 >> 24) & 15U) + 1ULL);
    result = -RT_EINVAL;
    if ((cfg1 & (1U << 19)) || !(control & 4U) || !(readl(pll + 3) & 1U) ||
        numerator != 1600000000ULL * denominator) goto out;

    /* Interrupts belong to CPU1 now, but handlers must not run during reset. */
    rt_hw_interrupt_mask(16 + 173);
    rt_hw_interrupt_mask(16 + 174);
    rt_hw_interrupt_mask(16 + 175);
    result = -RT_EIO;
    value = readl(cmu + AI_CLOCK / 4);
    if (value & AI_RATE_MASK) {
        writel(value & ~AI_GATES, cmu + AI_CLOCK / 4);
        if (readl(cmu + AI_CLOCK / 4) & AI_GATES) goto out;
        /* PLL0 / 2 / 1 = 800 MHz core; fixed PLL0 / 4 = 400 MHz AXI. */
        value &= ~(AI_GATES | AI_RATE_MASK);
        writel(value | (1U << 31), cmu + AI_CLOCK / 4);
        if (readl(cmu + AI_CLOCK / 4) & AI_RATE_MASK) goto out;
    }
    value = readl(cmu + AI_CLOCK / 4);
    writel(value | AI_GATES, cmu + AI_CLOCK / 4);
    if ((readl(cmu + AI_CLOCK / 4) & (AI_GATES | AI_RATE_MASK)) != AI_GATES) goto out;
    if (tdvp_cpu1_vision_ownership_status()) goto out;
    /* W1C the old done flag, assert only the AI reset, wait at most 100 ms.
     * AI2D/FFT share this reset: never use it as a per-job recovery action.
     */
    writel(readl(reset) | AI_RESET_DONE, reset);
    rt_thread_mdelay(1);
    if (readl(reset) & AI_RESET_DONE) goto out;
    writel(readl(reset) | 1U, reset);
    for (waited = 0; waited < 100; ++waited) {
        if (readl(reset) & AI_RESET_DONE) break;
        rt_thread_mdelay(1);
    }
    if (waited == 100) goto out;
    __sync_synchronize();
    if (readl(pll) != cfg0 || readl(pll + 1) != cfg1 ||
        !(readl(pll + 2) & 4U) || !(readl(pll + 3) & 1U) ||
        (readl(power + 0x2c / 4) & 3U) != 2U ||
        (readl(cmu + 0x60 / 4) & 0x50U) != 0x50U) goto out;
    result = tdvp_cpu1_vision_ownership_status();
out:
    if (reset_page) rt_iounmap((void *)reset_page);
    if (power) rt_iounmap((void *)power);
    if (pll) rt_iounmap((void *)pll);
    if (cmu) rt_iounmap((void *)cmu);
    if (result) rt_kprintf("TDVP CPU1: AI clock/reset preparation refused: %d\n", result);
    return result;
}
