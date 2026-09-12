/* SPDX-License-Identifier: MIT */
/* Execute the real clock helpers and the pinned SDK's range/page-count
 * functions. Only allocation, leaf PTE effects and hardware are modeled.
 * This is not an emulation of C908 traps, caches, TLBs or a board boot.
 */
#include "tdvp-cpu1-mmio-mock.h"
#include "sdk-page-size.inc"
#define ARCH_PAGE_SIZE (1UL << PAGE_OFFSET_BIT)
_Static_assert(ARCH_PAGE_SIZE == 4096, "revalidate the K230 mapping layout");
typedef struct { int unused; } rt_mmu_info;
static rt_mmu_info mmu_info;
static _Alignas(4096) uint32_t pages[8][4096 / sizeof(uint32_t)];
static unsigned int leaf[8]; /* 0=unmapped, 1=this mapping, 2=unrelated neighbor */
static struct {
    void *address;
    uintptr_t physical;
    size_t size;
    unsigned int slot;
    int live;
} mappings[4];
static unsigned int map_calls, allocated, unmapped, foreign_unmaps, flushes;
static unsigned int failed_map, irq_off, hardlock_held, reset_writes;
static int mode, refuse_grant, invalid_pll, reset_timeout, hardlock_timeout;
static uint64_t ticks;

static void *rt_hw_mmu_v2p(rt_mmu_info *info, void *address);
static void rt_hw_cpu_tlb_invalidate(void);
static void __rt_hw_mmu_unmap(rt_mmu_info *info, void *address, rt_size_t count);
void rt_hw_mmu_unmap(rt_mmu_info *info, void *address, rt_size_t size);
/* The vendor range walker compares an int counter to size_t. Keep its body
 * byte-for-byte; scope that existing warning to this SDK excerpt only.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "sdk-iounmap-range.inc"
#pragma GCC diagnostic pop
#include "sdk-mmu-unmap-range.inc"
#include "sdk-mmu-unmap-wrapper.inc"

static uintptr_t physical_address(const volatile void *address)
{
    uintptr_t value = (uintptr_t)address;
    for (unsigned int i = 0; i < allocated; ++i) {
        uintptr_t base = (uintptr_t)pages[mappings[i].slot];
        if (mappings[i].live && value >= base && value < base + ARCH_PAGE_SIZE)
            return (mappings[i].physical & ~(ARCH_PAGE_SIZE - 1)) + value - base;
    }
    assert(!"access outside a live mapping");
    return 0;
}

void *rt_ioremap(void *physical, unsigned long size)
{
    uintptr_t pa = (uintptr_t)physical, base = pa & ~(ARCH_PAGE_SIZE - 1);
    uintptr_t offset = pa - base;
    ++map_calls;
    if (map_calls == failed_map) return NULL;
    assert(allocated < 4 && size && offset + size <= ARCH_PAGE_SIZE);
    unsigned int i = allocated++, slot = i * 2;
    mappings[i].address = (unsigned char *)pages[slot] + offset;
    mappings[i].physical = pa;
    mappings[i].size = size;
    mappings[i].slot = slot;
    mappings[i].live = 1;
    leaf[slot] = 1;
    leaf[slot + 1] = 2; /* A second unmap must never reach this page. */
    uint32_t *words = pages[slot];
    switch (base) {
    case 0x91100000:
        words[0x08 / 4] = 0x401;
        words[0x24 / 4] = 0xffffffff;
        words[0x2c / 4] = 0x1b6d9249;
        words[0x60 / 4] = 0x50;
        break;
    case 0x91102000:
        words[0] = 0x0102018f + invalid_pll;
        words[1] = 0x200c7; words[2] = 0x40034; words[3] = 0x21;
        break;
    case 0x91103000: words[0x2c / 4] = 2; words[0x160 / 4] = 2; break;
    case 0x91101000: words[0x14 / 4] = 1U << 31; break;
    case 0x91104000: break;
    default: assert(!"unexpected MMIO page");
    }
    return mappings[i].address;
}

static void *rt_hw_mmu_v2p(rt_mmu_info *info, void *address)
{
    assert(info == &mmu_info);
    return (void *)physical_address(address);
}

static void __rt_hw_mmu_unmap(rt_mmu_info *info, void *address, rt_size_t count)
{
    assert(info == &mmu_info && irq_off);
    uintptr_t base = (uintptr_t)address & ~(ARCH_PAGE_SIZE - 1);
    assert(base >= (uintptr_t)pages);
    unsigned int slot = (unsigned int)((base - (uintptr_t)pages) / ARCH_PAGE_SIZE);
    assert(slot + count <= 8);
    for (rt_size_t n = 0; n < count; ++n) {
        if (leaf[slot + n] != 1) {
            ++foreign_unmaps;
            fprintf(stderr, "MMU unmap reached an unowned adjacent page (leaf=%u)\n", leaf[slot + n]);
        }
        leaf[slot + n] = 0;
    }
}

static void rt_hw_cpu_tlb_invalidate(void) { ++flushes; }

void rt_iounmap(void *address)
{
    assert(!irq_off && !hardlock_held);
    for (unsigned int i = 0; i < allocated; ++i) {
        if (mappings[i].live && mappings[i].address == address) {
            _iounmap_range(address, mappings[i].size);
            mappings[i].live = 0;
            ++unmapped;
            return;
        }
    }
    assert(!"iounmap must use the original mapping base");
}

rt_base_t rt_hw_interrupt_disable(void)
{ rt_base_t old = irq_off; irq_off = 1; return old; }
void rt_hw_interrupt_enable(rt_base_t old) { irq_off = (unsigned int)old; }
uint64_t tdvp_mmu_test_rdtime(void) { ticks += 27000; return ticks; }
int tdvp_cpu1_vision_ownership_status(void) { return refuse_grant ? -RT_EBUSY : 0; }
int rt_kprintf(const char *format, ...) { (void)format; return 0; }
void rt_hw_interrupt_mask(int irq) { assert(irq >= 189 && irq <= 191); }

uint32_t readl(const volatile void *address)
{
    uintptr_t pa = physical_address(address);
    if (pa == 0x911040a0) {
        assert(mode == 1 && irq_off && !hardlock_held);
        if (hardlock_timeout) return 1;
        hardlock_held = 1;
        return 0;
    }
    return *(const volatile uint32_t *)address;
}

void writel(uint32_t value, volatile void *address)
{
    uintptr_t pa = physical_address(address);
    if (pa == 0x911040a0) {
        assert(hardlock_held && irq_off && !value);
        hardlock_held = 0;
    } else if (pa == 0x91101014) {
        assert(mode == 2);
        ++reset_writes;
        assert(reset_writes <= 2);
        *(volatile uint32_t *)address = reset_writes == 1 ? 0 : 1;
    } else {
        assert(pa == 0x91100008 && mode == 2);
        *(volatile uint32_t *)address = value & ~(1U << 31);
    }
}

void rt_thread_mdelay(int delay)
{
    assert(delay == 1);
    if (reset_writes == 2 && !reset_timeout)
        for (unsigned int i = 0; i < allocated; ++i)
            if ((mappings[i].physical & ~(ARCH_PAGE_SIZE - 1)) == 0x91101000)
                pages[mappings[i].slot][0x14 / 4] = 1U << 31;
}

extern int tdvp_cpu1_i2c4_clock_prepare(void);
extern int tdvp_cpu1_ai_clock_prepare(void);
int main(int argc, char **argv)
{
    assert(argc == 3);
    mode = atoi(argv[1]);
    int scenario = atoi(argv[2]);
    assert(mode >= 1 && mode <= 4 && scenario >= 0 && scenario <= 8);
    if (mode >= 3) {
        /* Demonstrate both original offsets using the exact vendor range
         * functions, with an occupied and an absent neighboring PTE.
         */
        void *p = rt_ioremap((void *)(mode == 3 ? 0x911040a0UL : 0x91101014UL), 4);
        if (scenario) leaf[1] = 0;
        rt_iounmap(p);
        assert(foreign_unmaps == 1 && unmapped == 1);
        puts("PASS legacy unaligned map demonstrably reaches a second page");
        return 0;
    }
    if (scenario >= 1 && scenario <= (mode == 1 ? 3 : 4)) failed_map = scenario;
    if (scenario == 5) invalid_pll = 1;
    if (scenario == 6) { hardlock_timeout = mode == 1; reset_timeout = mode == 2; }
    if (scenario == 7) refuse_grant = mode == 2;
    int result = mode == 1 ? tdvp_cpu1_i2c4_clock_prepare() : tdvp_cpu1_ai_clock_prepare();
    if (failed_map) assert(result == -RT_ENOMEM);
    else if (invalid_pll) assert(result == -RT_EINVAL);
    else if (hardlock_timeout || reset_timeout || refuse_grant) assert(result < 0);
    else assert(!result);
    assert(!foreign_unmaps && !irq_off && !hardlock_held);
    assert(unmapped == allocated && flushes == allocated);
    for (unsigned int i = 0; i < allocated; ++i) {
        assert(!mappings[i].live && !leaf[i * 2]);
        assert(leaf[i * 2 + 1] == 2);
    }
    printf("PASS actual clock helper %d scenario %d: original bases only, %u pages released, neighbors retained\n",
           mode, scenario, allocated);
    return 0;
}
