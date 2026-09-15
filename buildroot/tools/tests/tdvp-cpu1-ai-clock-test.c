/* SPDX-License-Identifier: MIT */
#include "tdvp-cpu1-ai-mock.h"
#include "sysctl_media_clk.h"
#include "sysctl_rst.h"
#include "sysctl_pwr.h"

_Static_assert(offsetof(sysctl_media_clk_t, ai_clk_cfg)==8, "AI clock layout");
_Static_assert(offsetof(sysctl_media_clk_t, ddr_clk_cfg)==0x60, "DDR clock layout");
_Static_assert(offsetof(sysctl_rst_t, ai_rst_ctl)==0x14, "AI reset layout");
_Static_assert(offsetof(sysctl_pwr_s, ai_pwr_lpi_state)==0x2c, "AI power layout");
static uint32_t cmu[25], pll[4], power[89], reset_registers[0x1000 / 4];
#define reset_reg reset_registers[0x14 / 4]
static int scenario, maps, unmaps, writes, masks, delays, grant_calls, reset_writes;
extern int tdvp_cpu1_ai_clock_prepare(void);
int tdvp_cpu1_vision_ownership_status(void)
{ ++grant_calls; return scenario==1 || (scenario==20 && grant_calls>1) ? -RT_EBUSY : 0; }
int rt_kprintf(const char *fmt, ...) { (void)fmt; return 0; }
void *rt_ioremap(void *base, unsigned long size)
{
    ++maps;
    if (scenario>=2 && scenario<=5 && maps==scenario-1) return NULL;
    switch ((uintptr_t)base) {
    case 0x91100000: assert(size==0x64); return cmu;
    case 0x91102000: assert(size==0x10); return pll;
    case 0x91103000: assert(size==0x164); return power;
    case 0x91101000: assert(size==0x1000); return reset_registers;
    default: abort();
    }
}
void rt_iounmap(void *p) { assert(p==cmu || p==pll || p==power || p==reset_registers); ++unmaps; }
uint32_t readl(const volatile void *p)
{
    if (scenario==18 && reset_writes==2 && p==pll) return pll[0]^1U;
    if (scenario==19 && reset_writes==2 && p==power+0x2c/4) return 1;
    return *(const volatile uint32_t *)p;
}
void writel(uint32_t v, volatile void *p)
{
    ++writes;
    if (p==cmu+2) {
        assert((v & ~0x8000043dU)==(0x5a5a0000U & ~0x8000043dU));
        if (scenario==13 && !(v&0x401U)) return;
        if (scenario==14 && (v&(1U<<31))) return;
        if (scenario==15 && (v&0x401U)) return;
        if ((cmu[2]&0x3cU)!=(v&0x3cU)) assert(!(cmu[2]&0x401U));
        cmu[2]=v&0x7fffffffU;
    } else {
        assert(p==&reset_reg && masks==3); ++reset_writes;
        if(reset_writes==1) { assert(v&(1U<<31)); if(scenario!=16)reset_reg=0; }
        else { assert(reset_writes==2 && (v&1U)); reset_reg=1; }
    }
}
void rt_thread_mdelay(int ms)
{ assert(ms==1); ++delays; if(reset_writes==2 && scenario!=17)reset_reg=(1U<<31); }
void rt_hw_interrupt_mask(int irq) { assert(irq==189+masks); ++masks; }
int main(int argc, char **argv)
{
    assert(argc==2); scenario=atoi(argv[1]); assert(scenario<=21);
    cmu[2]=0x5a5a043d; cmu[24]=0x50; power[11]=2; power[88]=2;
    pll[0]=0x0102018f; pll[1]=0x200c7; pll[2]=0x40034; pll[3]=0x21; reset_reg=1U<<31;
    if(scenario==6)power[11]=1;
    if(scenario==7)power[88]=0;
    if(scenario==8)cmu[24]=0x10;
    if(scenario==9)pll[0]++;
    if(scenario==10)pll[1]|=1U<<19;
    if(scenario==11)pll[2]&=~4U;
    if(scenario==12)pll[3]=0;
    if(scenario==21)cmu[2]&=~0x3cU;
    int result=tdvp_cpu1_ai_clock_prepare();
    if(scenario==0 || scenario==21) assert(!result && (cmu[2]&0x43dU)==0x401U && reset_writes==2);
    else assert(result<0);
    if(scenario<=12 && scenario)assert(!writes && !masks);
    assert(unmaps==(scenario==1 ? 0 : scenario>=2 && scenario<=5 ? 3 : 4));
    assert(delays<=101);
    printf("CPU1 AI clock/reset: PASS case %d, dedicated writes only\n",scenario);
    return 0;
}
