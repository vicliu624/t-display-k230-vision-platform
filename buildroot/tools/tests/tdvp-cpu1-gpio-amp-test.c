/* SPDX-License-Identifier: MIT */
/* Execute the actual helper extracted from the Linux patch against a model
 * of the K230 read-to-acquire semaphore and live GPIO port registers.
 * This proves software ownership/RMW behavior, not physical lock timing.
 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef uint32_t u32;
#define BIT(n) (1UL << (n))
#define GPIO_SWPORTA_DR 0
#define GPIO_SWPORTA_DDR 4
#define REMOTE_BIT ((u32)BIT(21))
struct gpio_chip { pthread_mutex_t bgpio_lock; };
struct k230_gpio {
    void *regs, *amp_lock_reg, *dev;
    u32 amp_remote_mask;
};
struct k230_gpio_port { struct gpio_chip gc; struct k230_gpio *gpio; };
#define to_k230_gpio(gcptr) (((struct k230_gpio_port *)((char *)(gcptr) - offsetof(struct k230_gpio_port, gc)))->gpio)

static _Atomic u32 gpio_regs[2], hardware_lock, expected_linux[2], expected_cpu1[2];
static _Atomic unsigned int timeout_logs;
static _Thread_local int owns_lock, cpu1_writer, force_timeout;

static u32 readl(void *address)
{
    if (address == &hardware_lock) {
        u32 busy;
        if (force_timeout)
            return 1;
        busy = atomic_exchange_explicit(&hardware_lock, 1, memory_order_acquire);
        if (!busy) {
            assert(!owns_lock);
            owns_lock = 1;
        }
        return busy;
    }
    assert(address == &gpio_regs[0] || address == &gpio_regs[1]);
    return atomic_load((_Atomic u32 *)address);
}

static void writel(u32 value, void *address)
{
    unsigned int index;
    assert(owns_lock);
    if (address == &hardware_lock) {
        assert(!value);
        owns_lock = 0;
        atomic_store_explicit(&hardware_lock, 0, memory_order_release);
        return;
    }
    assert(address == &gpio_regs[0] || address == &gpio_regs[1]);
    index = address == &gpio_regs[1];
    if (cpu1_writer) {
        assert((value & ~REMOTE_BIT) == atomic_load(&expected_linux[index]));
        atomic_store(&expected_cpu1[index], value & REMOTE_BIT);
    } else {
        assert((value & REMOTE_BIT) == atomic_load(&expected_cpu1[index]));
        atomic_store(&expected_linux[index], value & ~REMOTE_BIT);
    }
    atomic_store((_Atomic u32 *)address, value);
}

#define mb() atomic_thread_fence(memory_order_seq_cst)
#define dev_err_ratelimited(device, ...) do { (void)(device); atomic_fetch_add(&timeout_logs, 1); } while (0)
#define raw_spin_lock_irqsave(lock, flags) do { assert(!owns_lock); (flags) = 0; assert(!pthread_mutex_lock(lock)); } while (0)
#define raw_spin_unlock_irqrestore(lock, flags) do { (void)(flags); assert(!owns_lock); assert(!pthread_mutex_unlock(lock)); } while (0)
#define readl_poll_timeout_atomic(addr, val, cond, delay, timeout) \
    ({ int status = -ETIMEDOUT; unsigned int attempts; \
       (void)(delay); (void)(timeout); \
       for (attempts = 0; attempts < 1000000; ++attempts) { \
           (val) = readl(addr); if (cond) { status = 0; break; } \
           if (force_timeout) break; sched_yield(); \
       } status; })

#include "gpio-k230-tdvp-amp.h"

static void *cpu1_thread(void *context)
{
    struct k230_gpio *gpio = context;
    unsigned int iteration, reg;
    cpu1_writer = 1;
    for (iteration = 0; iteration < 100000; ++iteration) {
        while (readl(gpio->amp_lock_reg))
            sched_yield();
        for (reg = 0; reg < 2; ++reg) {
            u32 value = readl(&gpio_regs[reg]);
            value = (value & ~REMOTE_BIT) | ((iteration & 1) ? REMOTE_BIT : 0);
            writel(value, &gpio_regs[reg]);
        }
        writel(0, gpio->amp_lock_reg);
    }
    return NULL;
}

int main(void)
{
    struct k230_gpio gpio = {.regs = gpio_regs, .amp_lock_reg = &hardware_lock, .amp_remote_mask = REMOTE_BIT};
    struct k230_gpio_port port = {.gc = {.bgpio_lock = PTHREAD_MUTEX_INITIALIZER}, .gpio = &gpio};
    unsigned long mask, bits, flags;
    unsigned int iteration;
    u32 before[2];
    pthread_t thread;

    gpio_regs[0] = REMOTE_BIT | BIT(31);
    gpio_regs[1] = REMOTE_BIT;
    expected_linux[0] = BIT(31);
    expected_linux[1] = 0;
    expected_cpu1[0] = expected_cpu1[1] = REMOTE_BIT;
    assert(k230_amp_direction_output(&port.gc, 21, 0) == -EBUSY);
    assert(k230_amp_direction_input(&port.gc, 21) == -EBUSY);
    k230_amp_set(&port.gc, 21, 0);
    assert(readl(&gpio_regs[0]) == (REMOTE_BIT | BIT(31)));

    assert(!pthread_create(&thread, NULL, cpu1_thread, &gpio));
    for (iteration = 0; iteration < 100000; ++iteration) {
        assert(!k230_amp_direction_output(&port.gc, 6, iteration & 1));
        assert(!k230_amp_get_direction(&port.gc, 6));
        k230_amp_set(&port.gc, 6, !(iteration & 1));
        mask = BIT(6) | BIT(9) | REMOTE_BIT;
        bits = (iteration & 1) ? mask : 0;
        k230_amp_set_multiple(&port.gc, &mask, &bits);
        assert(!k230_amp_direction_input(&port.gc, 6));
        assert(k230_amp_get_direction(&port.gc, 6) == 1);
    }
    assert(!pthread_join(thread, NULL));
    assert(!atomic_load(&timeout_logs));
    assert(!atomic_load(&hardware_lock));

    raw_spin_lock_irqsave(&port.gc.bgpio_lock, flags);
    k230_amp_restore(&gpio, GPIO_SWPORTA_DR, 0x55aa55aa);
    k230_amp_restore(&gpio, GPIO_SWPORTA_DDR, 0xaa55aa55);
    raw_spin_unlock_irqrestore(&port.gc.bgpio_lock, flags);
    assert((readl(&gpio_regs[0]) & ~REMOTE_BIT) == (0x55aa55aa & ~REMOTE_BIT));
    assert((readl(&gpio_regs[1]) & ~REMOTE_BIT) == (0xaa55aa55 & ~REMOTE_BIT));

    before[0] = readl(&gpio_regs[0]);
    before[1] = readl(&gpio_regs[1]);
    force_timeout = 1;
    k230_amp_set(&port.gc, 6, 1);
    assert(k230_amp_direction_output(&port.gc, 6, 1) == -ETIMEDOUT);
    raw_spin_lock_irqsave(&port.gc.bgpio_lock, flags);
    k230_amp_restore(&gpio, GPIO_SWPORTA_DR, 0);
    raw_spin_unlock_irqrestore(&port.gc.bgpio_lock, flags);
    assert(atomic_load(&timeout_logs) == 3);
    assert(before[0] == readl(&gpio_regs[0]) && before[1] == readl(&gpio_regs[1]));
    assert(!owns_lock && !atomic_load(&hardware_lock));
    puts("CPU1 GPIO AMP: PASS 100000 concurrent iterations per core, remote-bit protection, directions, restore and timeout refusal");
    return 0;
}
