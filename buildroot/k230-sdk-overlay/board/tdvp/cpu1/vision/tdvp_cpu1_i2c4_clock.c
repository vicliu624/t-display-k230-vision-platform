/* SPDX-License-Identifier: MIT */
#include <rtthread.h>
#include <rthw.h>
#include <ioremap.h>
#include <riscv_io.h>
#include <encoding.h>
#include <tick.h>
#include <stdint.h>

/* BOARD-init only, called once by the paired I2C4 hook. Do not use the
 * general clock API here: its BOARD initializer may not have mapped CMU yet.
 * Linux 0069 and CPU1 use HARDLOCK_GPIO0 (read zero acquires/write zero
 * releases). Never call a GPIO operation while holding this semaphore.
 */
#define CMU_PHYS 0x91100000UL
#define PLL_PHYS 0x91102000UL
#define LOCK_PHYS 0x911040a0UL
#define LS_GATE (0x24 / 4)
#define I2C_DIV (0x2c / 4)
#define I2C4_GATE (1U << 25)
#define I2C4_GATES (I2C4_GATE | (1U << 10) | 1U)
#define I2C4_DIV_MASK (7U << 27)
#define CLOCK_TIMEOUT_TICKS ((uint64_t)TIMER_CLK_FREQ / 100U)

_Static_assert(TIMER_CLK_FREQ == 27000000, "revalidate early hardlock timeout timebase");

int tdvp_cpu1_i2c4_clock_prepare(void)
{
    volatile uint32_t *cmu = RT_NULL, *pll = RT_NULL, *lock = RT_NULL;
    uint32_t cfg0, cfg1, ctl, state, divider, gates;
    uint64_t numerator, denominator, ratio, started;
    unsigned int polls;
    rt_base_t irq;
    int result = -RT_ENOMEM, acquired = 0;

    cmu = rt_ioremap((void *)CMU_PHYS, 0x34);
    if (!cmu) goto out;
    pll = rt_ioremap((void *)PLL_PHYS, 0x10);
    if (!pll) goto out;
    lock = rt_ioremap((void *)LOCK_PHYS, 4);
    if (!lock) goto out;

    irq = rt_hw_interrupt_disable();
    started = rdtime();
    result = -RT_ETIMEOUT;
    /* An iteration ceiling also refuses access if the timer stops. */
    for (polls = 0; polls < 1000000U; ++polls) {
        if (!readl(lock)) { acquired = 1; break; }
        if ((uint64_t)(rdtime() - started) >= CLOCK_TIMEOUT_TICKS) break;
    }
    if (!acquired) goto unlock;
    __sync_synchronize();

    cfg0 = readl(pll);
    cfg1 = readl(pll + 1);
    ctl = readl(pll + 2);
    state = readl(pll + 3);
    result = -RT_EINVAL;
    /* Observe boot PLL0; never retune/bypass/reset a shared PLL. I2C4's
     * parent is PLL0/4. The pinned DesignWare driver assumes IC_CLK=100 MHz.
     * Use exact rational arithmetic, not rounded MHz or floating point.
     */
    if ((cfg1 & (1U << 19)) || !(ctl & 4U) || (ctl & 1U) || !(state & 1U))
        goto unlock;
    numerator = 24000000ULL * ((cfg0 & 0x1fffU) + 1U);
    denominator = (((cfg0 >> 16) & 0x3fU) + 1ULL) *
                  (((cfg0 >> 24) & 0xfU) + 1ULL) * 4ULL * 100000000ULL;
    ratio = numerator / denominator;
    if (numerator % denominator || ratio < 1 || ratio > 8) goto unlock;
    divider = ((uint32_t)ratio - 1U) << 27;

    result = -RT_EIO;
    /* Only the paired Linux DT may relinquish I2C4. Gate its functional
     * clock before changing its divider, preserving every Linux-owned bit.
     * Do not retune the LS APB divider (0x30) or touch GPU/display clocks.
     */
    if ((readl(cmu + I2C_DIV) & I2C4_DIV_MASK) != divider) {
        gates = readl(cmu + LS_GATE);
        if (gates & I2C4_GATE) {
            writel(gates & ~I2C4_GATE, cmu + LS_GATE);
            if (readl(cmu + LS_GATE) & I2C4_GATE) goto unlock;
        }
        writel((readl(cmu + I2C_DIV) & ~I2C4_DIV_MASK) | divider | (1U << 31),
               cmu + I2C_DIV);
        if ((readl(cmu + I2C_DIV) & I2C4_DIV_MASK) != divider) goto unlock;
    }
    gates = readl(cmu + LS_GATE);
    if ((gates & I2C4_GATES) != I2C4_GATES)
        writel(gates | I2C4_GATES, cmu + LS_GATE);
    if ((readl(cmu + LS_GATE) & I2C4_GATES) != I2C4_GATES) goto unlock;
    if (readl(pll) != cfg0 || readl(pll + 1) != cfg1 ||
        readl(pll + 2) != ctl || !(readl(pll + 3) & 1U)) goto unlock;
    result = RT_EOK;

unlock:
    if (acquired) {
        __sync_synchronize();
        writel(0, lock);
    }
    rt_hw_interrupt_enable(irq);
out:
    if (lock) rt_iounmap((void *)lock);
    if (pll) rt_iounmap((void *)pll);
    if (cmu) rt_iounmap((void *)cmu);
    if (result)
        rt_kprintf("TDVP CPU1: I2C4 early clock refused: %d\n", result);
    return result;
}
