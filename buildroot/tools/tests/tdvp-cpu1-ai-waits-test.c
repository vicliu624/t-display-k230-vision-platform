/* SPDX-License-Identifier: MIT */
/* Real paired BSP callback bodies; only RT scheduling/event/MMIO environment
 * is simulated. No physical interrupt, DMA cancellation or inference claim. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <drv_hardlock.h>
#define RT_NULL NULL
#define RT_FALSE 0
#define RT_TRUE 1
#define RT_EINVAL 10
#define RT_EBUSY 7
#define RT_EIO 8
#define RT_ETIMEOUT 2
#define RT_EVENT_FLAG_OR 1
#define RT_EVENT_FLAG_CLEAR 4
#define POLLIN 1
#define POLLERR 8
#define GNNE_CMD_LOCK 0
#define GNNE_CMD_TRYLOCK 1
#define GNNE_CMD_UNLOCK 2
#define gnne_err(...) ((void)0)
#define ai_2d_err(...) ((void)0)
#define __iowmb() ((void)0)
typedef uint64_t rt_uint64_t;
typedef uint32_t rt_uint32_t;
typedef int rt_wqueue_t;
struct rt_pollreq { int unused; };
struct dfs_fd { void *data; };
struct rt_event { unsigned int bits; };
struct gnne_dev_handle { rt_wqueue_t *wait; int is_lock; };
struct ai_2d_dev_handle { rt_wqueue_t *wait; };
static struct rt_event g_gnne_event, g_ai_2d_event;
static rt_wqueue_t gnne_wait, ai2d_wait;
static hardlock_type g_kpu_lock = HARDLOCK_KPU;
static uint64_t mmio[0x1000 / 8];
static void *gnne_base_addr = mmio;
static int ai_state, owner_state, receive_error, lock_failures;
static int locks, unlocks, delays, adds, receives, wakes, lose_owner_at;

int tdvp_cpu1_ai_status(void) { return ai_state; }
int tdvp_cpu1_vision_runtime_status(void) { return owner_state; }
int kd_hardlock_lock(hardlock_type lock)
{
    assert(lock == HARDLOCK_KPU && !owner_state && !ai_state);
    ++locks;
    if (lock_failures) { --lock_failures; return -1; }
    return 0;
}
void kd_hardlock_unlock(hardlock_type lock) { assert(lock == HARDLOCK_KPU); ++unlocks; }
void rt_thread_mdelay(int ms)
{
    assert(ms == 1); ++delays;
    if (lose_owner_at && delays == lose_owner_at) owner_state = -RT_EIO;
}
static void rt_poll_add(rt_wqueue_t *wait, struct rt_pollreq *req)
{
    assert((wait == &gnne_wait || wait == &ai2d_wait) && req); ++adds;
}
static int rt_event_recv(struct rt_event *event, unsigned int mask, int flags, int timeout, void *unused)
{
    /* A zero-timeout caller must not be hidden inside a blocking event wait. */
    assert(timeout == 0 && adds && !unused && mask == 1);
    assert(flags == (RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR)); ++receives;
    if (receive_error) return receive_error;
    if (!(event->bits & mask)) return -RT_ETIMEOUT;
    event->bits &= ~mask;
    return 0;
}
static void rt_event_send(struct rt_event *event, unsigned int bits) { event->bits |= bits; }
static void rt_wqueue_wakeup(rt_wqueue_t *wait, void *key)
{
    /* Detect the old lost-wakeup order: readiness MUST exist before wake. */
    assert(key == (void *)POLLIN);
    assert(wait == &gnne_wait ? g_gnne_event.bits == 1 : g_ai_2d_event.bits == 1);
    ++wakes;
}
#include "gnne_device_ioctl.inc"
#include "gnne_device_poll.inc"
#include "ai_2d_device_poll.inc"
#define irq_callback test_gnne_irq
#include "gnne-irq.inc"
#undef irq_callback
#define irq_callback test_ai2d_irq
#include "ai2d-irq.inc"
#undef irq_callback

static void reset(void)
{
    ai_state = owner_state = receive_error = lock_failures = 0;
    locks = unlocks = delays = adds = receives = wakes = lose_owner_at = 0;
    g_gnne_event.bits = g_ai_2d_event.bits = 0;
    g_kpu_lock = HARDLOCK_KPU;
}
static void check_poll(int (*callback)(struct dfs_fd *, struct rt_pollreq *),
                       struct dfs_fd *file, struct rt_event *event)
{
    struct rt_pollreq req = {0};
    reset();
    assert(callback(file, &req) == 0 && adds == 1 && receives == 1 && !delays);
    event->bits = 1;
    assert(callback(file, &req) == POLLIN && !event->bits && !delays);
    assert(callback(file, &req) == 0);
    event->bits = 1; ai_state = -RT_EIO;
    assert(callback(file, &req) == POLLERR && event->bits == 1);
    ai_state = 0; owner_state = -RT_EIO;
    assert(callback(file, &req) == POLLERR && event->bits == 1);
    owner_state = -RT_EBUSY;
    assert(callback(file, &req) == 0 && event->bits == 1);
    owner_state = 0;
    assert(callback(file, &req) == POLLIN && !event->bits);
    receive_error = -RT_EIO;
    assert(callback(file, &req) == POLLERR);
    struct dfs_fd invalid = {0};
    assert(callback(&invalid, &req) == -EINVAL);
}
int main(void)
{
    struct gnne_dev_handle handle = {&gnne_wait, 0};
    struct ai_2d_dev_handle ai2d = {&ai2d_wait};
    struct dfs_fd file = {&handle}, second = {&ai2d}, invalid = {0};
    check_poll(gnne_device_poll, &file, &g_gnne_event);
    check_poll(ai_2d_device_poll, &second, &g_ai_2d_event);
    reset();
    test_gnne_irq(189, &gnne_wait); test_ai2d_irq(191, &ai2d_wait);
    assert(wakes == 2 && mmio[0x128 / 8] == 0x400000004ULL);
    assert(mmio[0xca0 / 8] == 1 && mmio[0xca8 / 8] == 0);
    reset();
    assert(gnne_device_ioctl(&invalid, GNNE_CMD_LOCK, NULL) == -RT_EINVAL);
    assert(gnne_device_ioctl(&file, 999, NULL) == -RT_EINVAL && !locks);
    lock_failures = 1000;
    assert(gnne_device_ioctl(&file, GNNE_CMD_TRYLOCK, NULL) == -RT_EBUSY && locks == 1 && !delays);
    reset(); lock_failures = 1000;
    assert(gnne_device_ioctl(&file, GNNE_CMD_LOCK, NULL) == -RT_ETIMEOUT);
    assert(locks == 100 && delays == 100 && !handle.is_lock && !unlocks);
    reset(); lock_failures = 3;
    assert(!gnne_device_ioctl(&file, GNNE_CMD_LOCK, NULL));
    assert(locks == 4 && delays == 3 && handle.is_lock);
    assert(!gnne_device_ioctl(&file, GNNE_CMD_LOCK, NULL) && locks == 4);
    assert(!gnne_device_ioctl(&file, GNNE_CMD_UNLOCK, NULL) && unlocks == 1 && !handle.is_lock);
    assert(!gnne_device_ioctl(&file, GNNE_CMD_UNLOCK, NULL) && unlocks == 1);
    reset(); lock_failures = 1000; lose_owner_at = 2;
    assert(gnne_device_ioctl(&file, GNNE_CMD_LOCK, NULL) == -RT_EIO);
    assert(locks == 2 && delays == 2 && !unlocks && !handle.is_lock);
    reset(); owner_state = -RT_EBUSY;
    assert(gnne_device_ioctl(&file, GNNE_CMD_TRYLOCK, NULL) == -RT_EBUSY && !locks && !delays);
    assert(gnne_device_ioctl(&file, GNNE_CMD_LOCK, NULL) == -RT_ETIMEOUT && !locks && delays == 100);
    reset(); ai_state = -RT_EIO;
    assert(gnne_device_ioctl(&file, GNNE_CMD_LOCK, NULL) == -RT_EIO && !locks && !delays);
    puts("CPU1 AI waits: PASS real callbacks, nonblocking poll, event-before-wakeup, bounded yielding lock and ownership refusal; no DMA/model acceptance");
    return 0;
}
