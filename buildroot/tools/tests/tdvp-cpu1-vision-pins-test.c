/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <rtthread.h>

static uint32_t pads[64], original[64];
static int map_fail, corrupt_pin, writes, unmapped;
static const unsigned int owned[] = {7, 8, 13, 21};
static const uint32_t expected[] = {0x11cf, 0x11cf, 0x088e, 0x018f};
extern int tdvp_cpu1_vision_pins_init(void);

void *rt_ioremap(void *physical, unsigned long bytes)
{
    assert(physical == (void *)0x91105000UL && bytes == 0x1000);
    return map_fail ? NULL : pads;
}
void rt_iounmap(void *mapped) { assert(mapped == pads); ++unmapped; }
uint32_t readl(const volatile void *address)
{
    unsigned long pin = (const volatile uint32_t *)address - pads;
    assert(pin < 64);
    return pads[pin];
}
void writel(uint32_t value, volatile void *address)
{
    unsigned long pin = (volatile uint32_t *)address - pads;
    assert(writes < 4 && pin == owned[writes++]);
    assert((value & ~0x3dffU) == (original[pin] & ~0x3dffU));
    pads[pin] = value ^ ((int)pin == corrupt_pin ? 1 : 0);
}
int rt_kprintf(const char *format, ...) { (void)format; return 0; }

int main(void)
{
    unsigned int test, pin, index;
    for (test = 0; test < 8; ++test) {
        for (pin = 0; pin < 64; ++pin)
            original[pin] = pads[pin] = 0x6a0055aaU ^ (pin * 0x13579U);
        original[46] = pads[46] = pads[46] & ~0x3800U;
        original[47] = pads[47] = pads[47] & ~0x3800U;
        if (test >= 6)
            original[test + 40] = pads[test + 40] |= 0x1800U;
        map_fail = test == 1;
        corrupt_pin = test >= 2 && test < 6 ? (int)owned[test - 2] : -1;
        writes = unmapped = 0;
        assert(tdvp_cpu1_vision_pins_init() == (map_fail ? -RT_ENOMEM :
               (test >= 6 ? -RT_EBUSY : (test >= 2 ? -RT_EIO : 0))));
        assert(unmapped == !map_fail);
        assert(writes == (test == 0 ? 4 : (test == 1 || test >= 6 ? 0 : (int)test - 1)));
        for (pin = 0; pin < 64; ++pin) {
            int is_owned = 0;
            for (index = 0; index < 4; ++index) {
                if (pin != owned[index]) continue;
                is_owned = 1;
                if (!test) assert((pads[pin] & 0x3dffU) == expected[index]);
            }
            if (!is_owned) assert(pads[pin] == original[pin]);
        }
    }
    puts("CPU1 camera pins: PASS four-pad whitelist, voltage/reserved bits, map/readback and alternate-pad conflicts");
    return 0;
}
