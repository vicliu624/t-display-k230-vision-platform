/* SPDX-License-Identifier: MIT */
/* Execute the real clock helper, board hook and patched BOARD entry point. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rtthread.h>
#include <riscv_io.h>
#include <ioremap.h>

static uint32_t cmu[32], before[32], pll[4], semaphore;
static unsigned int maps, unmaps, locks, releases, clock_writes, pll_reads;
static int scenario, held, irq_off, initialized, registered, speed_calls;
static int ownership;
static uint64_t timer;
struct i2c_regs { uint32_t ic_enable_status; };
static struct i2c_regs controller;
struct rt_i2c_bus_device { const void *ops; };
struct chip_i2c_bus {
    struct rt_i2c_bus_device parent;
    struct { struct i2c_regs *regs; } i2c;
    unsigned int clock;
    int slave;
    char *device_name;
};
static struct chip_i2c_bus i2c_buses[] = {
    {.i2c.regs = (void *)0x91409000UL, .device_name = "i2c4"}
};
static const int chip_i2c_ops = 42;
#define I2C_FAST_SPEED 400000
#define IC_CLK 100

void *rt_ioremap(void *address, unsigned long size)
{
    ++maps;
    if ((scenario >= 1 && scenario <= 3 && maps == (unsigned)scenario) ||
        (scenario == 16 && maps == 4)) return NULL;
    switch ((uintptr_t)address) {
    case 0x91100000: assert(size == 0x34); return cmu;
    case 0x91102000: assert(size == 16); return pll;
    case 0x911040a0: assert(size == 4); return &semaphore;
    case 0x91409000:
        assert(size == 0x10000 && !held && !irq_off && releases == 1);
        assert((cmu[9] & 0x02000401U) == 0x02000401U);
        return &controller;
    default: abort();
    }
}
void rt_iounmap(void *address)
{
    assert(!held && !irq_off);
    assert(address == cmu || address == pll || address == &semaphore);
    ++unmaps;
}
long rt_hw_interrupt_disable(void) { assert(!irq_off); irq_off = 1; return 123; }
void rt_hw_interrupt_enable(long level) { assert(level == 123 && irq_off && !held); irq_off = 0; }
uint64_t tdvp_test_rdtime(void)
{
    assert(irq_off);
    if (scenario != 20) timer += 27000;
    return timer;
}
uint32_t readl(const volatile void *address)
{
    if (address == &semaphore) {
        assert(irq_off && !held);
        ++locks;
        if (scenario == 4 || scenario == 20 || (scenario == 21 && locks == 1)) return 1;
        held = 1;
        return 0;
    }
    if (address == &controller.ic_enable_status) {
        assert(initialized && speed_calls == 1 && !held);
        return controller.ic_enable_status;
    }
    assert(held && irq_off);
    if ((uintptr_t)address >= (uintptr_t)pll && (uintptr_t)address < (uintptr_t)(pll + 4)) {
        ++pll_reads;
        return *(const volatile uint32_t *)address ^
               (scenario == 15 && pll_reads > 4 && address == pll ? 1U : 0U);
    }
    assert((uintptr_t)address >= (uintptr_t)cmu && (uintptr_t)address < (uintptr_t)(cmu + 32));
    return *(const volatile uint32_t *)address;
}
void writel(uint32_t value, volatile void *address)
{
    assert(held && irq_off);
    if (address == &semaphore) {
        assert(value == 0);
        held = 0; ++releases; return;
    }
    assert(address == cmu + 9 || address == cmu + 11);
    ++clock_writes;
    if (address == cmu + 9) {
        assert((value & ~0x02000401U) == (before[9] & ~0x02000401U));
        if (scenario == 13 && (value & (1U << 25))) return;
        if (scenario == 14 && !(value & (1U << 25))) return;
        cmu[9] = value;
    } else {
        assert(!(cmu[9] & (1U << 25)));
        assert((value & ~0xb8000000U) == (before[11] & ~0xb8000000U));
        assert(value & (1U << 31));
        if (scenario == 12) return;
        cmu[11] = value & ~(1U << 31); /* write-enable strobe is not data */
    }
}
static void dw_i2c_init(struct i2c_regs *regs)
{
    assert(regs == &controller && !initialized && !held && !irq_off);
    initialized = 1;
    controller.ic_enable_status = scenario == 18 ? 0 : 1;
}
static int designware_i2c_set_bus_speed(struct chip_i2c_bus *bus, unsigned int speed)
{
    assert(initialized && bus == i2c_buses && bus->clock == 100000000 && speed == 400000);
    ++speed_calls;
    return scenario == 17 ? -123 : 0;
}
static int rt_i2c_bus_device_register(struct rt_i2c_bus_device *bus, const char *name)
{
    assert(bus == &i2c_buses[0].parent && bus->ops == &chip_i2c_ops && !strcmp(name, "i2c4"));
    assert(initialized && speed_calls == 1 && controller.ic_enable_status == 1);
    ++registered;
    return scenario == 19 ? -124 : 0;
}
int rt_kprintf(const char *format, ...) { (void)format; return 0; }
int tdvp_cpu1_vision_ownership_status(void) { return ownership ? 0 : -RT_EBUSY; }
#include "tdvp_cpu1_i2c4_board.h"
#include "production-i2c-entry.h"

int main(int argc, char **argv)
{
    assert(argc == 2);
    scenario = atoi(argv[1]);
    assert(scenario >= 0 && scenario <= 26);
    for (unsigned int i = 0; i < 32; ++i) cmu[i] = 0x65432100U ^ (i * 1337U);
    cmu[9] &= ~0x02000401U;
    cmu[11] &= ~0xb8000000U;
    if (scenario == 11 || scenario == 14) cmu[9] |= 1U << 25;
    if (scenario == 22) { cmu[9] |= 0x02000401U; cmu[11] |= 3U << 27; }
    if (scenario == 26) {
        /* Read-only snapshot from the board, 2026-09-07; no device writes. */
        cmu[9] = 0xffffffffU; cmu[11] = 0x1b6d9249U; cmu[12] = 0x01dda0c3U;
    }
    memcpy(before, cmu, sizeof(cmu));
    pll[0] = 199U | (2U << 16); /* 24 MHz * 200 / 3 = 1600 MHz */
    pll[1] = 0; pll[2] = 4; pll[3] = 1;
    if (scenario == 26) {
        pll[0] = 0x0102018fU; pll[1] = 0x000200c7U;
        pll[2] = 0x00040034U; pll[3] = 0x00000021U;
    }
    if (scenario == 5) pll[1] = 1U << 19;
    if (scenario == 6) pll[2] = 0;
    if (scenario == 7) pll[2] = 5;
    if (scenario == 8) pll[3] = 0;
    if (scenario == 9) pll[0] = 200U | (2U << 16);
    if (scenario == 10) pll[0] = 999U | (2U << 16);
    if (scenario == 23) pll[0] = 99U | (2U << 16); /* 800 MHz / 4 / 2 */
    if (scenario == 21) timer = UINT64_MAX - 40000;
    if (scenario == 24) i2c_buses[0].slave = 1;
    if (scenario == 25) i2c_buses[0].i2c.regs = (void *)0x91405000UL;
    int expected = 0;
    if ((scenario >= 1 && scenario <= 3) || scenario == 16) expected = -RT_ENOMEM;
    if (scenario == 4 || scenario == 20) expected = -RT_ETIMEOUT;
    if ((scenario >= 5 && scenario <= 10) || scenario == 24 || scenario == 25) expected = -RT_EINVAL;
    if ((scenario >= 12 && scenario <= 15) || scenario == 18) expected = -RT_EIO;
    if (scenario == 17) expected = -123;
    if (scenario == 19) expected = -124;
    assert(tdvp_cpu1_i2c4_init_status() == -RT_EBUSY);
    assert(rt_hw_i2c_init() == 0); /* BOARD entry must not touch ANY hardware. */
    assert(!maps && !clock_writes && !initialized && !registered);
    assert(tdvp_cpu1_i2c4_board_init() == -RT_EBUSY);
    assert(!maps && !clock_writes && !initialized && !registered);
    ownership = 1;
    assert(tdvp_cpu1_i2c4_board_init() == expected);
    assert(tdvp_cpu1_i2c4_init_status() == expected);
    unsigned int saved_maps = maps, saved_writes = clock_writes, saved_locks = locks;
    assert(rt_hw_i2c_init() == 0);
    assert(tdvp_cpu1_i2c4_board_init() == expected); /* no repeated map/register/reset */
    assert(maps == saved_maps && clock_writes == saved_writes && locks == saved_locks);
    assert(!held && !irq_off);
    assert(unmaps == (scenario == 24 || scenario == 25 ? 0U : (scenario >= 1 && scenario <= 3 ? (unsigned)scenario - 1 : 3U)));
    assert(releases == (scenario == 24 || scenario == 25 || (scenario >= 1 && scenario <= 4) || scenario == 20 ? 0U : 1U));
    if ((scenario >= 1 && scenario <= 10) || scenario == 20 || scenario == 24 || scenario == 25)
        assert(!clock_writes && !initialized && !registered);
    if (scenario >= 12 && scenario <= 16) assert(!initialized && !registered);
    if (scenario == 17 || scenario == 18) assert(!registered);
    if (!expected) {
        assert(registered == 1 && initialized && speed_calls == 1);
        assert((cmu[11] & 0x38000000U) == (scenario == 23 ? 1U << 27 : 3U << 27));
    }
    if (scenario == 22 || scenario == 26) assert(!clock_writes);
    if (scenario == 20) assert(locks == 1000000U);
    if (scenario == 21) assert(locks == 2);
    for (unsigned int i = 0; i < 32; ++i)
        if (i != 9 && i != 11) assert(cmu[i] == before[i]);
    printf("CPU1 I2C4 early init: PASS case %d status %d\n", scenario, expected);
    return 0;
}
