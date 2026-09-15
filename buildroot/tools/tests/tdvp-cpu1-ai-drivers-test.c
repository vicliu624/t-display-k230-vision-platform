/* SPDX-License-Identifier: MIT */
/* Execute initializer bodies extracted from zero-fuzz patched pinned BSP.
 * RT services are mocked; the production ordering/error paths are not copied.
 */
#include "tdvp-cpu1-ai-mock.h"
#include <board.h>
#include <drv_hardlock.h>
#define IRQN_GNNE_INTERRUPT 189
#define IRQN_AI2D_INTERRUPT 191
struct device_hardlock { volatile void *hw_base; char used[HARDLOCK_MAX]; };
static struct device_hardlock hardlock;
static struct rt_device g_gnne_device, g_ai_2d_device;
static struct rt_event g_gnne_event, g_ai_2d_event;
static const struct dfs_file_ops gnne_input_fops = {0}, ai_2d_input_fops = {0};
static hardlock_type g_kpu_lock = HARDLOCK_MAX;
static void *gnne_base_addr;
static unsigned char mailbox[MAILBOX_IO_SIZE], ai_mmio[KPU_IO_SIZE+FFT_IO_SIZE+AI2D_IO_SIZE];
static int scenario, granted, calls, event_count, queue_count, registrations;
static int installed[2], masked[2], unmasked[2];
static void irq_callback(int irq, void *data) { (void)irq; (void)data; }
#include "hardlock-init.inc"
#include "gnne-init.inc"
#include "ai2d-init.inc"

int tdvp_cpu1_vision_ownership_status(void) { return granted ? 0 : -RT_EBUSY; }
int rt_kprintf(const char *fmt, ...) { (void)fmt; return 0; }
void *rt_ioremap(void *p, unsigned long size)
{
    ++calls;
    if ((uintptr_t)p==MAILBOX_BASE_ADDR) {
        assert(size==MAILBOX_IO_SIZE); return scenario==1 ? NULL : mailbox;
    }
    assert((uintptr_t)p==KPU_BASE_ADDR && size==sizeof(ai_mmio));
    return scenario==3 ? NULL : ai_mmio;
}
int kd_request_lock(hardlock_type lock)
{
    ++calls; assert(lock==HARDLOCK_KPU && tdvp_cpu1_hardlock_ready());
    return scenario==2 ? -1 : 0;
}
static int which_irq(int irq) { assert(irq==189 || irq==191); return irq==191; }
void rt_hw_interrupt_mask(int irq) { ++calls; masked[which_irq(irq)]++; }
void rt_hw_interrupt_umask(int irq)
{
    int i=which_irq(irq); ++calls;
    assert(masked[i] && installed[i] && registrations==i+1);
    struct rt_device *dev=i ? &g_ai_2d_device : &g_gnne_device;
    assert(dev->fops==(i ? &ai_2d_input_fops : &gnne_input_fops) && dev->wait_queue==1);
    unmasked[i]++;
}
int rt_event_init(struct rt_event *e, const char *name, int flags)
{
    ++calls; ++event_count;
    assert(gnne_base_addr==ai_mmio && name && flags==RT_IPC_FLAG_PRIO);
    assert(e==(event_count==1 ? &g_gnne_event : &g_ai_2d_event));
    return (scenario==4 && event_count==1) || (scenario==6 && event_count==2) ? -21 : 0;
}
void rt_wqueue_init(rt_wqueue_t *queue)
{
    ++calls; ++queue_count;
    assert(queue==(queue_count==1 ? &g_gnne_device.wait_queue : &g_ai_2d_device.wait_queue));
    assert(!*queue); *queue=1;
}
void rt_hw_interrupt_install(int irq, void (*fn)(int,void *),void *data,const char *name)
{
    int i=which_irq(irq); ++calls;
    struct rt_device *dev=i ? &g_ai_2d_device : &g_gnne_device;
    assert(fn==irq_callback && data==&dev->wait_queue && name && queue_count==i+1);
    assert(dev->fops==(i ? &ai_2d_input_fops : &gnne_input_fops));
    installed[i]++;
}
int rt_device_register(rt_device_t dev,const char *name,int flags)
{
    int i=dev==&g_ai_2d_device; ++calls;
    assert(dev==(i ? &g_ai_2d_device : &g_gnne_device) && !unmasked[i]);
    assert(flags==RT_DEVICE_FLAG_RDWR && !strcmp(name,i ? "ai_2d_device" : "gnne_device"));
    if ((scenario==5 && !i) || (scenario==7 && i)) return -22;
#include "fft-register-posix.inc"
    ++registrations; return 0;
}
int main(int argc,char **argv)
{
    assert(argc==2); scenario=atoi(argv[1]); assert(scenario>=0 && scenario<=8);
    assert(gnne_device_init()==-RT_EBUSY && ai_2d_device_init()==-RT_EBUSY && !calls);
    assert(!tdvp_cpu1_hardlock_ready());
    int mapped=rt_hw_hardlock_init();
    assert(scenario==1 ? mapped<0 && !tdvp_cpu1_hardlock_ready() : !mapped && hardlock.hw_base==mailbox+0xa0);
    granted=1;
    if(scenario==8) {
        assert(ai_2d_device_init()<0 && !registrations && !unmasked[1]);
        puts("CPU1 AI drivers: PASS AI2D refuses missing GNNE mapping"); return 0;
    }
    int result=gnne_device_init();
    if(scenario>=1 && scenario<=5) {
        assert(result<0 && !registrations && !unmasked[0]);
        int before=calls; assert(gnne_device_init()==result && calls==before);
    } else {
        assert(!result && unmasked[0]==1 && g_kpu_lock==HARDLOCK_KPU);
        result=ai_2d_device_init();
        assert(scenario ? result<0 && registrations==1 && !unmasked[1] : !result && registrations==2 && unmasked[1]==1);
        int before=calls; assert(ai_2d_device_init()==result && !gnne_device_init() && calls==before);
    }
    printf("CPU1 AI drivers: PASS real patched initializer case %d, no early device/IRQ publication\n",scenario);
    return 0;
}
