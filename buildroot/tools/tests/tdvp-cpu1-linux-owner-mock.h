/* SPDX-License-Identifier: MIT */
#ifndef TDVP_LINUX_OWNER_MOCK_H
#define TDVP_LINUX_OWNER_MOCK_H
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define __iomem
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t __u32;
typedef int32_t __s32;
typedef uint64_t __u64;
typedef uint64_t phys_addr_t;
typedef uint64_t resource_size_t;
#define EPROBE_DEFER 517
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
extern unsigned int warnings;
#define WARN_ON(x) ((x) ? (++warnings, 1) : 0)
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
struct device_node { const char *path; bool available, nomap, reusable, registered; u64 base, size; };
struct device { struct device_node *of_node; };
struct clk { unsigned int id; };
struct clk_bulk_data { const char *id; struct clk *clk; };
struct resource { u64 start, end; };
struct reserved_mem { u64 base, size; };
struct of_phandle_args { struct device_node *np; unsigned int args_count, args[1]; };
#define resource_size(r) ((r)->end - (r)->start + 1)
int of_property_match_string(struct device_node *, const char *, const char *);
struct device_node *of_parse_phandle(struct device_node *, const char *, int);
struct reserved_mem *of_reserved_mem_lookup(struct device_node *);
bool of_device_is_available(struct device_node *);
bool of_property_read_bool(struct device_node *, const char *);
int of_property_read_u32(struct device_node *, const char *, u32 *);
int of_address_to_resource(struct device_node *, int, struct resource *);
void of_node_put(struct device_node *);
struct device_node *of_find_node_by_path(const char *);
int of_count_phandle_with_args(struct device_node *, const char *, const char *);
int of_property_count_strings(struct device_node *, const char *);
void *of_find_property(struct device_node *, const char *, int *);
int of_parse_phandle_with_args(struct device_node *, const char *, const char *, int, struct of_phandle_args *);
void *devm_ioremap(struct device *, phys_addr_t, size_t);
struct resource *request_mem_region(resource_size_t, resource_size_t, const char *);
void release_mem_region(resource_size_t, resource_size_t);
struct device *dev_pm_domain_attach_by_name(struct device *, const char *);
void dev_pm_domain_detach(struct device *, bool);
int pm_runtime_resume_and_get(struct device *);
void pm_runtime_put_sync(struct device *);
int clk_bulk_get(struct device *, int, struct clk_bulk_data *);
void clk_bulk_put(int, struct clk_bulk_data *);
int clk_bulk_prepare_enable(int, struct clk_bulk_data *);
void clk_bulk_disable_unprepare(int, struct clk_bulk_data *);
int clk_rate_exclusive_get(struct clk *);
void clk_rate_exclusive_put(struct clk *);
unsigned long clk_get_rate(struct clk *);
u64 ktime_get(void);
#define ktime_to_ms(t) (t)
u64 get_random_u64(void);
u32 readl(const u32 *);
u64 readq(const u64 *);
void writel(u32, u32 *);
void writeq(u64, u64 *);
#define mb() __sync_synchronize()
#endif
