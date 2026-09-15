/* SPDX-License-Identifier: MIT */
/* Test the actual driver and pinned ioctl ABI; this is NOT numerical silicon validation. */
#include "tdvp-cpu1-ai-mock.h"
#include "tdvp_cpu1_fft.c"
static uint64_t regs[128];
static int scenario, owned, stage, fail_stage, writes, allocated, input_count, output_count, total_jobs;
static int armed, irq_enabled, faulted, wait_calls, installed, registered;
static int publication_waits;
static int plic_claimed, plic_completions;
static void (*handler)(int,void *);
static int failure(void) { return ++stage==fail_stage; }
int tdvp_cpu1_vision_ownership_status(void) { return owned ? 0 : -RT_EBUSY; }
int tdvp_cpu1_vision_runtime_status(void)
{
    if (wait_calls && (scenario==17 || (scenario==16 && publication_waits<2))) return -RT_EBUSY;
    return scenario==13 && wait_calls ? -RT_EIO : 0;
}
void rt_thread_mdelay(int ms) { assert(ms==1); ++publication_waits; }
void tdvp_cpu1_ai_fail(void) { ++faulted; }
void *rt_ioremap(void *p, unsigned long s)
{ assert((uintptr_t)p==FFT_BASE_ADDR && s==FFT_IO_SIZE); return failure() ? NULL : regs; }
void rt_hw_interrupt_mask(int irq) { assert(irq==190); irq_enabled=0; }
void rt_hw_interrupt_umask(int irq) { assert(irq==190); irq_enabled=1; }
void rt_hw_interrupt_install(int irq,void (*fn)(int,void *),void *p,const char *name)
{ assert(irq==190 && !p && name); handler=fn; ++installed; }
int rt_mutex_init(struct rt_mutex *m,const char *n,int f)
{ (void)m; assert(n && f==1); return failure() ? -RT_EIO : 0; }
int rt_event_init(struct rt_event *e,const char *n,int f)
{ (void)e; assert(n && f==1); return failure() ? -RT_EIO : 0; }
void rt_wqueue_init(rt_wqueue_t *q) { assert(!*q); *q = 1; }
int rt_device_register(rt_device_t dev,const char *n,int f)
{
    assert(!strcmp(n,"fft_device") && f==3 && installed && !irq_enabled);
    if(failure())return -RT_EIO;
    /* Execute the POSIX initialization block from the actual pinned SDK. */
#include "fft-register-posix.inc"
    ++registered; return 0;
}
int rt_mutex_take(struct rt_mutex *m,int timeout)
{ assert(timeout==100 && !m->locked); if(scenario==6)return -RT_EBUSY; m->locked=1; return 0; }
int rt_mutex_release(struct rt_mutex *m) { assert(m->locked); m->locked=0; return 0; }
void *rt_malloc(size_t n) { if(scenario==7)return NULL; ++allocated; return malloc(n); }
void rt_free(void *p) { assert(allocated); --allocated; free(p); }
size_t lwp_get_from_user(void *d,const void *s,size_t n) { if(scenario==8)return 0; memcpy(d,s,n); return n; }
size_t lwp_put_to_user(void *d,const void *s,size_t n) { if(scenario==14)return 0; memcpy(d,s,n); return n; }
static unsigned int offset(const volatile void *p)
{ uintptr_t v=(uintptr_t)p-(uintptr_t)regs; assert(v<FFT_IO_SIZE && !(v&7)); return v; }
void writeq(uint64_t v,volatile void *p)
{
    unsigned int o=offset(p); ++writes;
    assert(o==0 || o==0x10 || o==0x20 || o==0x40);
    if(o==0x40) { assert(armed); ++input_count; return; }
    if(o==0x10) { assert(v<=1); armed=v; if(v) { input_count=output_count=wait_calls=0; ++total_jobs; } }
    regs[o/8]=v;
}
uint64_t readq(const volatile void *p)
{
    unsigned int o=offset(p); assert(o==0x40 || o==0x50);
    if(o==0x40) { assert(armed); ++output_count; return 0x1234000000000000ULL+output_count; }
    return scenario==12 ? 2 : scenario==15 && output_count ? 1 : 0;
}
int rt_event_send(struct rt_event *e,unsigned int v) { e->bits|=v; return 0; }
int rt_event_recv(struct rt_event *e,unsigned int bits,int flags,int timeout,void *received)
{
    assert(bits==1 && flags==(RT_EVENT_FLAG_OR|RT_EVENT_FLAG_CLEAR) && !received);
    if(!timeout) { e->bits=0; return -RT_ETIMEOUT; }
    assert(timeout==50 && armed); ++wait_calls;
    if(scenario==11)return -RT_ETIMEOUT;
    assert(irq_enabled);
    /* A gateway cannot forward another IRQ until the previous claim has
     * been completed. PLIC ignores completion while this source is disabled.
     * Pinned generic_handle_irq() completes only after the ISR returns. */
    if(plic_claimed)return -RT_ETIMEOUT;
    plic_claimed=1;
    handler(190,NULL);
    if(irq_enabled) { plic_claimed=0; ++plic_completions; }
    assert(e->bits==1); e->bits=0; return 0;
}
int main(int argc,char **argv)
{
    k_fft_args_st args;
    assert(argc==2); scenario=atoi(argv[1]);
    if(scenario>=1 && scenario<=4)fail_stage=scenario;
    assert(tdvp_cpu1_fft_init()==-RT_EBUSY && !stage && !writes);
    owned=1; int result=tdvp_cpu1_fft_init();
    if(fail_stage) { assert(result<0 && !registered && !irq_enabled); assert(tdvp_cpu1_fft_init()==result); return 0; }
    assert(!result && registered==1 && !irq_enabled && fft_device.fops==&fft_ops);
    assert(fft_device.wait_queue==1 && !fft_device.fops->open(NULL));
    int before=writes;
    memset(&args,0,sizeof(args));
    if(scenario==5)args.reg.cfg_value=7;
    if(scenario==9)args.reg.cfg_value=3<<4;
    if(scenario==10)args.reg.cfg_value=0x80;
    if(scenario) {
        result=fft_ioctl(NULL,KD_IOC_CMD_FFT_IFFT,&args);
        assert((scenario==16 ? !result : result<0) && !allocated && !fft_mutex.locked && !armed);
        if(scenario>=5 && scenario<=10)assert(writes==before && !faulted);
        if(scenario==11 || scenario==12 || scenario==13 || scenario==15 || scenario==17) {
            assert(faulted==1 && fft_open(NULL)<0);
            before=writes; assert(fft_ioctl(NULL,KD_IOC_CMD_FFT_IFFT,&args)<0 && writes==before);
        }
        if(scenario==11)assert(wait_calls==10);
        if(scenario==14)assert(!faulted);
        if(scenario==16)assert(!faulted && publication_waits==2);
        if(scenario==17)assert(publication_waits==3);
    } else {
        for(unsigned int point=0;point<=6;++point)
        for(unsigned int im=0;im<=2;++im)
        for(unsigned int om=0;om<=1;++om)
        for(unsigned int mode=0;mode<=1;++mode) {
            memset(&args,0,sizeof(args));
            args.reg.cfg_value=point | (im<<4) | (om<<6) | (mode<<3);
            assert(!fft_ioctl(NULL,KD_IOC_CMD_FFT_IFFT,&args));
            assert(input_count==(int)((64U<<point)/(im==RRRR ? 4U : 2U)));
            assert(output_count==(int)((64U<<point)/2U) && !armed && !irq_enabled);
            assert(args.data[output_count-1]==0x1234000000000000ULL+output_count);
            assert((regs[0]>>32)==FFT_HW_TIMEOUT);
        }
        assert(total_jobs==84 && plic_completions==84 && !plic_claimed && !allocated && !faulted);
    }
    assert(!fft_close(NULL));
    printf("CPU1 FFT PIO: PASS scenario %d (hardware FIFO model, not FFT accuracy)\n",scenario);
}
