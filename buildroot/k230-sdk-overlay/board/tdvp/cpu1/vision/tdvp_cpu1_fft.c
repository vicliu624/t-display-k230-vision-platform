/* SPDX-License-Identifier: MIT */
/* K230 hardware FFT, with the pinned MPI ioctl ABI. CPU1 PIO transfers avoid
 * taking the system SDMA away from Linux. Computation remains hardware FFT.
 * No shared AI reset, arbitrary physical address, DMA allocation, or retry
 * after hardware/ownership failure. One bounded job at a time.
 */
#include <rtthread.h>
#include <rthw.h>
#include <dfs_posix.h>
#include <lwp_user_mm.h>
#include <ioremap.h>
#include <riscv_io.h>
#include <board.h>
#include <k_fft_ioctl.h>

#define FFT_IRQ (16 + 174)
#define FFT_EVENT 1U
#define FFT_HW_TIMEOUT 80000000U /* at most 100 ms at the pinned 800 MHz clock */
extern int tdvp_cpu1_vision_ownership_status(void);
extern int tdvp_cpu1_vision_runtime_status(void);
extern void tdvp_cpu1_ai_fail(void);

static struct rt_device fft_device;
static struct rt_event fft_event;
static struct rt_mutex fft_mutex;
static volatile unsigned char *fft_registers;
static volatile rt_uint64_t fft_interrupt_code;
static int fft_attempted;
static volatile int fft_status = -RT_EBUSY;

static void fft_write(unsigned int offset, rt_uint64_t value)
{
    writeq(value, (volatile void *)(fft_registers + offset));
    __sync_synchronize();
}
static rt_uint64_t fft_read(unsigned int offset)
{
    rt_uint64_t value = readq((const volatile void *)(fft_registers + offset));
    __sync_synchronize();
    return value;
}
static void fft_stop(void)
{
    rt_hw_interrupt_mask(FFT_IRQ);
    fft_write(0x10, 0); /* Only this FFT engine's enable, never AI reset. */
    fft_write(0x20, 1);
}
static void fft_irq(int irq, void *data)
{
    (void)irq; (void)data;
    rt_hw_interrupt_mask(FFT_IRQ);
    fft_interrupt_code = fft_read(0x50);
    fft_write(0x20, 1);
    rt_event_send(&fft_event, FFT_EVENT);
}
static int fft_runtime_ready(void)
{
    int result, attempt;
    /* A sequence publication in progress is not an ownership failure. Let
     * the owner thread finish it, without accepting stale/FAULT records or
     * extending this into an unbounded wait. No owner-state mutation here.
     */
    for (attempt = 0; attempt < 4; ++attempt) {
        result = tdvp_cpu1_vision_runtime_status();
        if (result != -RT_EBUSY || attempt == 3) return result;
        rt_thread_mdelay(1);
    }
    return -RT_EBUSY;
}
static int fft_open(struct dfs_fd *file)
{
    (void)file;
    if (fft_status) return fft_status;
    return fft_runtime_ready();
}
static int fft_close(struct dfs_fd *file) { (void)file; return 0; }

static int fft_ioctl(struct dfs_fd *file, int command, void *argument)
{
    k_fft_args_st *args;
    rt_uint64_t config;
    unsigned int point, input_mode, input_words, output_words, i;
    int result, waited;
    (void)file;

    /* DFS passes an int even when the SDK _IOWR macro has unsigned-long
     * type. Compare the 32-bit ABI value, not a sign-extended promotion.
     */
    if ((rt_uint32_t)command != (rt_uint32_t)KD_IOC_CMD_FFT_IFFT || !argument)
        return -RT_EINVAL;
    if (fft_status) return fft_status;
    if (rt_mutex_take(&fft_mutex, rt_tick_from_millisecond(100))) return -RT_EBUSY;
    args = rt_malloc(sizeof(*args));
    if (!args) { result = -RT_ENOMEM; goto unlock; }
    if (lwp_get_from_user(args, argument, sizeof(*args)) != sizeof(*args)) {
        result = -RT_EINVAL; goto free;
    }
    config = args->reg.cfg_value;
    point = config & 7U;
    input_mode = (config >> 4) & 3U;
    /* Reserved fields and interrupt masking are not user-controllable. */
    if (point > FFT_N4096 || input_mode > RR_II || (config & 0xffe00080ULL)) {
        result = -RT_EINVAL; goto free;
    }
    result = fft_runtime_ready();
    if (fft_status) result = fft_status;
    if (result) goto free;
    output_words = (64U << point) / 2U;
    input_words = input_mode == RRRR ? output_words / 2U : output_words;
    if (!(config >> 32) || (config >> 32) > FFT_HW_TIMEOUT)
        config = (config & 0xffffffffULL) | ((rt_uint64_t)FFT_HW_TIMEOUT << 32);

    fft_stop();
    (void)rt_event_recv(&fft_event, FFT_EVENT, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 0, RT_NULL);
    fft_interrupt_code = 0;
    fft_write(0, config);
    rt_hw_interrupt_umask(FFT_IRQ);
    fft_write(0x10, 1);
    for (i = 0; i < input_words; ++i) {
        if (fft_interrupt_code) { result = -RT_EIO; goto fault; }
        fft_write(0x40, args->data[i]);
    }
    for (waited = 0; waited < 10; ++waited) {
        result = rt_event_recv(&fft_event, FFT_EVENT, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                               rt_tick_from_millisecond(50), RT_NULL);
        if (fft_runtime_ready()) { result = -RT_EIO; goto fault; }
        if (!result) break;
        if (result != -RT_ETIMEOUT) goto fault;
    }
    if (waited == 10) { result = -RT_ETIMEOUT; goto fault; }
    if (fft_interrupt_code) { result = -RT_EIO; goto fault; }
    for (i = 0; i < output_words; ++i) args->data[i] = fft_read(0x40);
    /* Catch an output FIFO fault even though the completion ISR masked IRQ. */
    if (fft_read(0x50)) { result = -RT_EIO; goto fault; }
    fft_stop();
    result = lwp_put_to_user(argument, args, sizeof(*args)) == sizeof(*args) ? 0 : -RT_EINVAL;
    goto free;
fault:
    fft_stop();
    fft_status = result ? result : -RT_EIO;
    tdvp_cpu1_ai_fail();
free:
    rt_free(args); /* No DMA ever references this PIO staging allocation. */
unlock:
    rt_mutex_release(&fft_mutex);
    return result;
}
static const struct dfs_file_ops fft_ops = { .open=fft_open, .close=fft_close, .ioctl=fft_ioctl };

int tdvp_cpu1_fft_init(void)
{
    int result;
    if (fft_attempted) return fft_status;
    result = tdvp_cpu1_vision_ownership_status();
    if (result) return result;
    fft_attempted = 1;
    rt_hw_interrupt_mask(FFT_IRQ);
    fft_status = -RT_ENOMEM;
    fft_registers = rt_ioremap((void *)FFT_BASE_ADDR, FFT_IO_SIZE);
    if (!fft_registers) return fft_status;
    result = rt_mutex_init(&fft_mutex, "tdvp_fft", RT_IPC_FLAG_PRIO);
    if (result) return fft_status = result;
    result = rt_event_init(&fft_event, "tdvp_fft", RT_IPC_FLAG_PRIO);
    if (result) return fft_status = result;
    fft_stop();
    fft_device.fops = &fft_ops;
    rt_hw_interrupt_install(FFT_IRQ, fft_irq, RT_NULL, "tdvp_fft");
    result = rt_device_register(&fft_device, "fft_device", RT_DEVICE_FLAG_RDWR);
    /* IRQ remains masked until a validated job starts. No library SDMA init. */
    return fft_status = result;
}
