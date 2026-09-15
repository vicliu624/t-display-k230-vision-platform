/* SPDX-License-Identifier: MIT */
/* Real boot decompression control flow; MMIO, DMA setup and time are mocked. */
#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned long ulong;
enum { DMA_CH_0, DMA_CH_1, UGZIP_RD=0, UGZIP_WR=1 };
#define BIT(n) (1U << (n))
#include "boot-types.inc"
static struct ugzip_reg gzip_regs;
static struct gsdma_ctrl dma_regs;
static uint32_t channel_regs[24]; /* Two hardware channels, 0x30-byte stride. */
static unsigned char descriptor_r, descriptor_w;
static void *g_llt_list_r, *g_llt_list_w;
#define UGZIP_BASE_ADDR ((uintptr_t)&gzip_regs)
#define GSDMA_CTRL_ADDR ((uintptr_t)&dma_regs)
#define SDMA_CH_CFG ((uintptr_t)channel_regs)
enum { NORMAL, DELAYED, RD_STUCK, WR_STUCK, GZIP_STUCK, MAP_STUCK,
       CRC_ERROR, DECOMP_TIMEOUT, TIMER_WRAP, RD_DELAYED, CASE_COUNT };
static unsigned int scenario, stops, idle_reads, map_reads, free_count, reset_writes;
static unsigned int busy_reads[2], delay_calls, timer_calls, tick_calls, invalidated;
static ulong milliseconds;
static jmp_buf halted;

static ulong boot_test_timer(ulong base)
{
    ++timer_calls;
    assert(timer_calls <= 102);
    return milliseconds++ - base; /* U-Boot elapsed-time unsigned wrap semantics. */
}
static uint64_t boot_test_ticks(void)
{
    return (uint64_t)tick_calls++ * 80000001;
}
static void boot_test_delay(unsigned int microseconds)
{
    assert(microseconds == 1 && stops == 3);
    ++delay_calls;
}
static uint32_t boot_test_readl(const volatile void *address)
{
    uintptr_t target=(uintptr_t)address;
    if (target == (uintptr_t)&dma_regs.dma_int_stat)
        return scenario == DECOMP_TIMEOUT ? 0 : 2;
    if (target == (uintptr_t)&gzip_regs.decomp_intstat)
        return scenario == CRC_ERROR ? 0 : BIT(10);
    if (target == 0x91101054 || target == 0x9110105c) {
        assert(!free_count);
        return target == 0x91101054 ? BIT(29) : BIT(31);
    }
    for (unsigned int ch=0; ch<2; ++ch) {
        if (target == SDMA_CH_CFG + ch * SDMA_CH_LENGTH + 4) {
            assert(stops == 3 && !free_count && !map_reads);
            ++busy_reads[ch];
            if ((scenario == RD_STUCK && !ch) || (scenario == WR_STUCK && ch)) return 1;
            if ((scenario == DELAYED || scenario == TIMER_WRAP ||
                 (scenario == RD_DELAYED && !ch)) && busy_reads[ch] <= 3) return 1;
            idle_reads |= 1U << ch;
            return 0;
        }
    }
    if (target == (uintptr_t)&gzip_regs.gzip_src_size) {
        assert(stops == 3 && idle_reads == 3 && !free_count);
        map_reads |= 1;
        return scenario == GZIP_STUCK ? BIT(31) : gzip_regs.gzip_src_size;
    }
    if (target == SDMA_CH_CFG + 8) {
        assert(stops == 3 && idle_reads == 3 && !free_count);
        map_reads |= 2;
        return scenario == MAP_STUCK ? BIT(10) : channel_regs[2];
    }
    assert(!"unexpected MMIO read");
    return 0;
}
static void boot_test_writel(uint32_t value, volatile void *address)
{
    uintptr_t target=(uintptr_t)address;
    assert(!free_count);
    if (target == 0x91101054 || target == 0x9110105c) {
        assert(scenario == CRC_ERROR || scenario == DECOMP_TIMEOUT);
        assert(value == 2 || value == 1 || value == BIT(29) || value == BIT(31));
        ++reset_writes;
        return;
    }
    if (target == 0x91302310) { assert(value == 0x51f); return; } /* Unchanged vendor NOC setup. */
    if (target == (uintptr_t)&dma_regs.dma_int_stat) return;
    if (target == (uintptr_t)&gzip_regs.gzip_src_size) {
        if (!value) assert(stops == 3 && idle_reads == 3);
        gzip_regs.gzip_src_size=value;
        return;
    }
    if (target == (uintptr_t)&gzip_regs.dma_out_size ||
        target == (uintptr_t)&gzip_regs.decomp_start) {
        *(volatile uint32_t *)address=value;
        return;
    }
    for (unsigned int ch=0; ch<2; ++ch) {
        if (target == SDMA_CH_CFG + ch * SDMA_CH_LENGTH) {
            assert(value == 2 && !(stops & (1U << ch)));
            stops |= 1U << ch;
            return;
        }
    }
    if (target == SDMA_CH_CFG + 8) {
        assert(!value && stops == 3 && idle_reads == 3);
        channel_regs[2]=value;
        return;
    }
    assert(!"unexpected MMIO write, including shared clocks/power or sibling DMA");
}
static int ugzip_sdma_cfg(unsigned int channel, unsigned int mode,
                           unsigned char *address, uint32_t length)
{
    (void)address;
    assert(channel < 2 && channel == mode && length && !free_count);
    if (!channel) g_llt_list_r=&descriptor_r;
    else g_llt_list_w=&descriptor_w;
    return 0;
}
static void boot_test_free(void *address)
{
    assert(stops == 3 && idle_reads == 3 && map_reads == 3);
    assert(address == (free_count ? (void *)&descriptor_w : (void *)&descriptor_r));
    assert((scenario == CRC_ERROR || scenario == DECOMP_TIMEOUT) ? reset_writes == 4 : !reset_writes);
    ++free_count;
}
static void boot_test_invalidate(uint64_t start, uint64_t end)
{
    assert(start && end > start && free_count == 2 && map_reads == 3);
    ++invalidated;
}
static _Noreturn void boot_test_hang(void)
{
    assert(!free_count && !invalidated && g_llt_list_r && g_llt_list_w);
    longjmp(halted,1);
}
#define readl boot_test_readl
#define writel boot_test_writel
#define get_timer boot_test_timer
#define get_ticks boot_test_ticks
#define udelay boot_test_delay
#define free boot_test_free
#define hang boot_test_hang
#define flush_dcache_range(start, end) ((void)(start), (void)(end))
#define invalidate_dcache_range boot_test_invalidate
#include "boot-body.inc"

int main(void)
{
    unsigned char source[32], destination[64];
    for (scenario=0; scenario<CASE_COUNT; ++scenario) {
        ulong bytes=sizeof(source);
        stops=idle_reads=map_reads=free_count=reset_writes=0;
        delay_calls=timer_calls=tick_calls=invalidated=0;
        busy_reads[0]=busy_reads[1]=0;
        milliseconds=scenario == TIMER_WRAP ? ULONG_MAX-2 : 1000;
        g_llt_list_r=g_llt_list_w=NULL;
        memset(&gzip_regs,0,sizeof(gzip_regs));
        memset(channel_regs,0,sizeof(channel_regs));
        if (!setjmp(halted)) {
            int result=k230_priv_unzip(destination,sizeof(destination),source,&bytes);
            assert(scenario != RD_STUCK && scenario != WR_STUCK && scenario != GZIP_STUCK && scenario != MAP_STUCK);
            assert(result == (scenario == CRC_ERROR ? 1 : scenario == DECOMP_TIMEOUT ? 2 : 0));
            assert(free_count == 2 && invalidated == 1 && !g_llt_list_r && !g_llt_list_w);
        } else {
            assert(scenario == RD_STUCK || scenario == WR_STUCK || scenario == GZIP_STUCK || scenario == MAP_STUCK);
            assert(!free_count && !invalidated && stops == 3);
            if (scenario == RD_STUCK || scenario == WR_STUCK) assert(!map_reads && timer_calls == 101);
        }
    }
    puts("TDVP boot decompressor: PASS 10 actual-body lifecycle cases; channel waits, timer wrap, mapping readback and halt-before-free; not hardware acceptance");
    return 0;
}
