/* SPDX-License-Identifier: MIT */
#ifndef TDVP_AI_MOCK_H
#define TDVP_AI_MOCK_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#define RT_NULL NULL
#define RT_EOK 0
#define RT_ETIMEOUT 2
#define RT_ENOMEM 5
#define RT_EBUSY 7
#define RT_EIO 8
#define RT_EINVAL 10
#define RT_IPC_FLAG_PRIO 1
#define RT_DEVICE_FLAG_RDWR 3
#define RT_EVENT_FLAG_OR 1
#define RT_EVENT_FLAG_CLEAR 4
typedef uint64_t rt_uint64_t;
typedef uint32_t rt_uint32_t;
typedef int rt_tick_t;
struct dfs_fd { int unused; };
struct dfs_file_ops {
    int (*open)(struct dfs_fd *);
    int (*close)(struct dfs_fd *);
    int (*ioctl)(struct dfs_fd *, int, void *);
};
typedef int rt_wqueue_t;
struct rt_device { const struct dfs_file_ops *fops; rt_wqueue_t wait_queue; };
typedef struct rt_device *rt_device_t;
struct rt_event { unsigned int bits; };
struct rt_mutex { int locked; };
int rt_kprintf(const char *, ...);
void *rt_ioremap(void *, unsigned long);
void rt_iounmap(void *);
uint32_t readl(const volatile void *);
void writel(uint32_t, volatile void *);
uint64_t readq(const volatile void *);
void writeq(uint64_t, volatile void *);
void rt_thread_mdelay(int);
void rt_hw_interrupt_mask(int);
void rt_hw_interrupt_umask(int);
void rt_hw_interrupt_install(int, void (*)(int, void *), void *, const char *);
int rt_mutex_init(struct rt_mutex *, const char *, int);
int rt_mutex_take(struct rt_mutex *, int);
int rt_mutex_release(struct rt_mutex *);
int rt_event_init(struct rt_event *, const char *, int);
int rt_event_recv(struct rt_event *, unsigned int, int, int, void *);
int rt_event_send(struct rt_event *, unsigned int);
int rt_device_register(rt_device_t, const char *, int);
void rt_wqueue_init(rt_wqueue_t *);
void *rt_malloc(size_t);
void rt_free(void *);
size_t lwp_get_from_user(void *, const void *, size_t);
size_t lwp_put_to_user(void *, const void *, size_t);
#define rt_tick_from_millisecond(ms) (ms)
#endif
