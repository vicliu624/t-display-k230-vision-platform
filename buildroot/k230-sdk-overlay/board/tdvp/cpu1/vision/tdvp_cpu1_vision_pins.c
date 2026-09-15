/* SPDX-License-Identifier: MIT */
#include <rtthread.h>
#include <ioremap.h>
#include <riscv_io.h>
#include <stdint.h>

/* Register layout and function selectors come from the pinned LilyGO
 * sample_display/fpioa/rt_fpioa.c. Do NOT use its global function selector:
 * that helper clears other pads carrying a duplicate function. CPU1 owns
 * exactly these four pads; preserve pad voltage bit 9 and reserved bits.
 */
#define TDVP_CAMERA_IOMUX_BASE 0x91105000UL
#define TDVP_CAMERA_PAD_MASK 0x3dffU

int tdvp_cpu1_vision_pins_init(void)
{
    static const struct { unsigned int pin; uint32_t config; } camera_pads[] = {
        {7,  0x11cfU}, /* selector 2: I2C4 SCL, input/output, pull-up */
        {8,  0x11cfU}, /* selector 2: I2C4 SDA, input/output, pull-up */
        {13, 0x088eU}, /* selector 1: MCLK1, output */
        {21, 0x018fU}, /* selector 0: GPIO21 reset; direction handled by GPIO */
    };
    volatile uint32_t *iomux = rt_ioremap((void *)TDVP_CAMERA_IOMUX_BASE, 0x1000);
    unsigned int index;
    int result = RT_EOK;

    if (!iomux)
        return -RT_ENOMEM;
    /* I2C4 also routes through pads 46/47 at selector 3. Fail instead of
     * clearing an alternate pad which may belong to Linux or the bootloader.
     */
    if (((readl(iomux + 46) >> 11) & 7U) == 3U ||
        ((readl(iomux + 47) >> 11) & 7U) == 3U) {
        rt_kprintf("TDVP CPU1 camera: I2C4 already routed through pads 46/47\n");
        rt_iounmap((void *)iomux);
        return -RT_EBUSY;
    }
    for (index = 0; index < sizeof(camera_pads) / sizeof(camera_pads[0]); ++index) {
        volatile uint32_t *reg = iomux + camera_pads[index].pin;
        uint32_t before = readl(reg);
        uint32_t value = (before & ~TDVP_CAMERA_PAD_MASK) | camera_pads[index].config;

        writel(value, reg);
        if ((readl(reg) & TDVP_CAMERA_PAD_MASK) != camera_pads[index].config) {
            rt_kprintf("TDVP CPU1 camera: pin %u configuration readback failed\n", camera_pads[index].pin);
            result = -RT_EIO;
            break;
        }
    }
    rt_iounmap((void *)iomux);
    return result;
}
