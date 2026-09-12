/* SPDX-License-Identifier: MIT */
/* Model register ownership around the actual patched Linux clock operations.
 * Concurrency and error behavior are tested; physical timing is not claimed.
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
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
typedef pthread_mutex_t spinlock_t;
#define __iomem
#define BIT(n) (1UL << (n))
#define U32_MAX UINT32_MAX
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#undef abs
#define abs(x) ((x) < 0 ? -(x) : (x))
struct clk_hw { int unused; };
struct list_head { struct list_head *next, *prev; };
struct device_node { bool enabled; uintptr_t start; int error; struct device_node *child, *next; };
static struct list_head tdvp_registered_clocks = {&tdvp_registered_clocks, &tdvp_registered_clocks};
static pthread_mutex_t tdvp_registered_clocks_lock = PTHREAD_MUTEX_INITIALIZER;
#define mutex_lock(p) assert(!pthread_mutex_lock(p))
#define mutex_unlock(p) assert(!pthread_mutex_unlock(p))
#define for_each_available_child_of_node(parent, node) for ((node)=(parent)->child; (node); (node)=(node)->next)
#define list_for_each_entry(pos, head, member) \
    for (struct list_head *entry=(head)->next; entry!=(head) && ((pos)=container_of(entry, __typeof__(*(pos)), member), 1); entry=entry->next)
static bool of_device_is_compatible(struct device_node *node, const char *name)
{ (void)node; assert(!strcmp(name,"canaan,k230-clk-composite")); return true; }
static void of_node_put(struct device_node *node) { assert(node); }
struct resource { uintptr_t start, end; };
static spinlock_t local_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic u32 cmu[64], hardware_lock, linux_bits[64], cpu1_bits[64];
static _Atomic unsigned int timeout_logs, writes;
static _Thread_local bool owns_lock, cpu1_writer, force_timeout;
static bool mapping_fails, require_hardlock = true;

static unsigned int reg_index(void *address)
{
    uintptr_t offset = (uintptr_t)address - (uintptr_t)cmu;
    assert(offset < sizeof(cmu) && !(offset & 3));
    return offset / 4;
}

static u32 remote_mask(unsigned int index)
{
    if (index == 0x24 / 4) return BIT(10) | BIT(25);
    if (index == 0x2c / 4) return 0x7U << 27;
    return 0;
}

static u32 readl(void *address)
{
    if (address == &hardware_lock) {
        u32 busy;
        if (force_timeout) return 1;
        busy = atomic_exchange_explicit(&hardware_lock, 1, memory_order_acquire);
        if (!busy) { assert(!owns_lock); owns_lock = true; }
        return busy;
    }
    return atomic_load(&cmu[reg_index(address)]);
}

static void writel(u32 value, void *address)
{
    unsigned int index;
    u32 mask;
    if (address == &hardware_lock) {
        assert(owns_lock && !value);
        owns_lock = false;
        atomic_store_explicit(&hardware_lock, 0, memory_order_release);
        return;
    }
    index = reg_index(address);
    mask = remote_mask(index);
    if (require_hardlock && (index == 0x24 / 4 || index == 0x2c / 4 || index == 0x30 / 4)) {
        assert(owns_lock);
        /* Divider bit 31 is a write strobe, not a retained divider field. */
        if (index != 0x24 / 4) value &= ~BIT(31);
        if (cpu1_writer) {
            assert((value & ~mask) == atomic_load(&linux_bits[index]));
            atomic_store(&cpu1_bits[index], value & mask);
        } else {
            assert((value & mask) == atomic_load(&cpu1_bits[index]));
            atomic_store(&linux_bits[index], value & ~mask);
        }
    }
    atomic_fetch_add(&writes, 1);
    atomic_store(&cmu[index], value);
}

static bool of_property_read_bool(struct device_node *node, const char *name)
{
    assert(!strcmp(name, "tdvp,cpu1-i2c4-clock-sharing"));
    return node->enabled;
}
static int of_address_to_resource(struct device_node *node, int index, struct resource *res)
{
    assert(!index);
    res->start = node->start; res->end = node->start + 0xfff;
    return node->error;
}
#define resource_size(res) ((res)->end - (res)->start + 1)
static void *ioremap(uintptr_t address, size_t size)
{
    assert(address == 0x911040a0 && size == 4);
    return mapping_fails ? NULL : &hardware_lock;
}
#define mb() atomic_thread_fence(memory_order_seq_cst)
#define pr_err(...) do {} while (0)
#define pr_err_ratelimited(...) atomic_fetch_add(&timeout_logs, 1)
#define clk_hw_get_name(hw) "test-clock"
#define spin_lock_irqsave(lock, flags) do { assert(!owns_lock); (flags) = 0; assert(!pthread_mutex_lock(lock)); } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(flags); assert(!owns_lock); assert(!pthread_mutex_unlock(lock)); } while (0)
#define readl_poll_timeout_atomic(addr, val, cond, delay, timeout) \
    ({ int status = -ETIMEDOUT; unsigned int attempts; \
       assert((delay) == 1 && (timeout) == 10000); \
       for (attempts = 0; attempts < 1000000; ++attempts) { \
           (val) = readl(addr); if (cond) { status = 0; break; } \
           if (force_timeout) { break; } \
           sched_yield(); \
       } status; })

#include "production-clock-ops.h"

static struct k230_clk_composite uart_clock(void)
{
    struct k230_clk_composite clk = {
        .base_reg = cmu, .gate_reg = &cmu[0x24 / 4], .gate_bit = 16,
        .rate_reg = &cmu[0x2c / 4], .rate_div_mask = 7, .rate_div_shift = 0,
        .rate_write_enable_bit = 31, .rate_calc_method = 1,
        .rate_mul_min = 1, .rate_mul_max = 1, .rate_div_min = 1, .rate_div_max = 8,
        .composite_spinlock = &local_lock,
    };
    return clk;
}

static void *cpu1_thread(void *unused)
{
    unsigned int iteration, index;
    (void)unused;
    cpu1_writer = true;
    for (iteration = 0; iteration < 100000; ++iteration) {
        while (readl(&hardware_lock)) sched_yield();
        for (index = 0x24 / 4; index <= 0x2c / 4; index += 2) {
            u32 mask = remote_mask(index);
            u32 value = readl(&cmu[index]);
            value = (value & ~mask) | ((iteration & 1) ? mask : 0);
            writel(value, &cmu[index]);
        }
        writel(0, &hardware_lock);
    }
    return NULL;
}

static void readiness_test(void)
{
    struct device_node parent={.enabled=true}, children[21]={0};
    struct k230_clk_composite registered[21];
    unsigned int i;

    assert(!k230_tdvp_clock_ready(NULL));
    assert(!k230_tdvp_clock_ready(&parent));
    parent.child=&children[0];
    for(i=0;i<21;++i) {
        registered[i]=uart_clock();
        registered[i].amp_lock_reg=&hardware_lock;
        registered[i].owner_node=&children[i];
        if(i<20) children[i].next=&children[i+1];
        registered[i].owner_entry.prev=tdvp_registered_clocks.prev;
        registered[i].owner_entry.next=&tdvp_registered_clocks;
        tdvp_registered_clocks.prev->next=&registered[i].owner_entry;
        tdvp_registered_clocks.prev=&registered[i].owner_entry;
    }
    /* Twenty guarded LS clocks plus one independent Linux GPU clock. */
    registered[20].gate_reg=&cmu[0x74/4]; registered[20].rate_reg=NULL;
    registered[20].amp_lock_reg=NULL;
    assert(k230_tdvp_clock_ready(&parent));
    for(i=0;i<20;++i) {
        registered[i].amp_lock_reg=NULL;
        assert(!k230_tdvp_clock_ready(&parent));
        registered[i].amp_lock_reg=&hardware_lock;
        registered[i].owner_node=NULL;
        assert(!k230_tdvp_clock_ready(&parent));
        registered[i].owner_node=&children[i];
    }
    parent.enabled=false; assert(!k230_tdvp_clock_ready(&parent));
    parent.enabled=true; parent.child=&children[1]; assert(!k230_tdvp_clock_ready(&parent));
    tdvp_registered_clocks.next=tdvp_registered_clocks.prev=&tdvp_registered_clocks;
    puts("CPU1 clock readiness: PASS actual getter, all enabled providers registered, all shared writers guarded; GPU unaffected");
}

int main(void)
{
    struct device_node parent = {.enabled = true, .start = 0x91100000};
    struct k230_clk_composite uart = uart_clock(), altered;
    pthread_t cpu1;
    unsigned int i, before;

    assert(!k230_clk_amp_setup(&uart, &parent));
    assert(uart.amp_lock_reg == &hardware_lock);
    assert(!pthread_create(&cpu1, NULL, cpu1_thread, NULL));
    for (i = 0; i < 100000; ++i) {
        assert(!k230_clk_composite_enable(&uart.hw));
        assert(!k230_clk_composite_set_rate(&uart.hw, (i & 1) ? 100000000 : 50000000, 400000000));
        k230_clk_composite_disable(&uart.hw);
    }
    assert(!pthread_join(cpu1, NULL));
    assert(!timeout_logs && !hardware_lock && !owns_lock);
    assert((readl(uart.rate_reg) & 7) == 3);

    before = writes; force_timeout = true;
    assert(k230_clk_composite_enable(&uart.hw) == -ETIMEDOUT);
    assert(k230_clk_composite_set_rate(&uart.hw, 100000000, 400000000) == -ETIMEDOUT);
    k230_clk_composite_disable(&uart.hw);
    assert(timeout_logs == 3 && writes == before && !owns_lock && !hardware_lock);
    force_timeout = false;

    altered = uart_clock(); altered.gate_bit = 0; altered.rate_reg = &cmu[0x30 / 4];
    assert(!k230_clk_amp_setup(&altered, &parent));
    assert(!k230_clk_composite_enable(&altered.hw));
    before = writes;
    k230_clk_composite_disable(&altered.hw);
    assert(k230_clk_composite_set_rate(&altered.hw, 100000000, 400000000) == -EBUSY);
    assert(writes == before && (readl(altered.gate_reg) & BIT(0)));

    /* PDM's gate shares 0x24, but its 0x40/0x44 fractional divider is Linux-only. */
    altered = uart_clock(); altered.gate_bit = 31;
    altered.rate_reg = &cmu[0x40 / 4]; altered.rate_reg_1 = &cmu[0x44 / 4];
    altered.rate_calc_method = 2; altered.rate_div_mask = 0x1ffff;
    altered.rate_mul_mask_1 = 0xffff; altered.rate_write_enable_bit_1 = 31;
    assert(!k230_clk_amp_setup(&altered, &parent));
    assert(!k230_clk_composite_enable(&altered.hw));
    assert(!k230_clk_composite_set_rate(&altered.hw, 128000, 400000000));
    assert(readl(altered.rate_reg) == 3125 && (readl(altered.rate_reg_1) & 0xffff) == 1);
    k230_clk_composite_disable(&altered.hw);

    altered = uart_clock(); altered.gate_bit = 10;
    assert(k230_clk_amp_setup(&altered, &parent) == -EINVAL);
    altered.gate_bit = 25;
    assert(k230_clk_amp_setup(&altered, &parent) == -EINVAL);
    altered = uart_clock(); altered.rate_div_shift = 27;
    assert(k230_clk_amp_setup(&altered, &parent) == -EINVAL);
    altered = uart_clock(); altered.mux_reg = altered.rate_reg;
    assert(k230_clk_amp_setup(&altered, &parent) == -EINVAL);
    altered = uart_clock(); altered.rate_reg_1 = &cmu[0x38 / 4];
    assert(k230_clk_amp_setup(&altered, &parent) == -EINVAL);
    altered = uart_clock(); altered.rate_div_shift = 32;
    assert(k230_clk_amp_setup(&altered, &parent) == -EINVAL);
    altered = uart_clock(); altered.rate_div_shift = 30;
    assert(k230_clk_amp_setup(&altered, &parent) == -EINVAL);
    altered = uart_clock(); altered.gate_bit_reverse = 1;
    assert(k230_clk_amp_setup(&altered, &parent) == -EINVAL);
    altered = uart_clock(); parent.start = 0x91101000;
    assert(k230_clk_amp_setup(&altered, &parent) == -EINVAL);
    parent.start = 0x91100000; mapping_fails = true;
    assert(k230_clk_amp_setup(&altered, &parent) == -ENOMEM);
    assert(!altered.amp_lock_reg); mapping_fails = false;

    /* The GPU/display register does not acquire CPU1's semaphore. */
    altered = uart_clock(); altered.gate_reg = &cmu[0x74 / 4]; altered.rate_reg = NULL;
    assert(!k230_clk_amp_setup(&altered, &parent) && !altered.amp_lock_reg);
    force_timeout = true;
    assert(!k230_clk_composite_enable(&altered.hw));
    k230_clk_composite_disable(&altered.hw);
    assert(timeout_logs == 3);
    /* No DT opt-in: even the LS path keeps its existing single-core behavior. */
    parent.enabled = false; altered = uart_clock(); require_hardlock = false;
    assert(!k230_clk_amp_setup(&altered, &parent) && !altered.amp_lock_reg);
    assert(!k230_clk_composite_enable(&altered.hw));
    k230_clk_composite_disable(&altered.hw);
    assert(!k230_clk_composite_set_rate(&altered.hw, 100000000, 400000000));
    assert(timeout_logs == 3 && !owns_lock && !hardware_lock);
    readiness_test();
    puts("CPU1 clock AMP: PASS 100000 concurrent iterations per core, real CCF ops, CPU1 field protection, APB retention, timeout refusal, malformed DT rejection and unchanged GPU/non-AMP paths");
    return 0;
}
