/* SPDX-License-Identifier: MIT */
/* Execute the complete patched driver. Linux and MMIO are simulated; this
 * does not verify silicon power/repair timing. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dt-bindings/soc/canaan,k230_pm_domains.h>
typedef uint16_t u16;
typedef uint32_t u32;
#define __iomem
#define __init
#define BIT(n) (1U << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define GFP_KERNEL 0
#define IORESOURCE_MEM 0
#define GENPD_FLAG_ALWAYS_ON BIT(2)
#define IS_ERR(p) ((intptr_t)(p) < 0 && (intptr_t)(p) > -4096)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define pr_info(...) ((void)0)
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define dev_info(...) ((void)0)
#define subsys_initcall(fn) static int (*test_init_entry)(void) = fn
#define EXPORT_SYMBOL_GPL(fn)
#define THIS_MODULE NULL
#define __module_get(module) (++module_pins)
#define smp_load_acquire(p) (*(p))
#define smp_store_release(p, v) (*(p) = (v))
#define of_node_get(node) (node)
struct device_node { bool vision; };
struct device { struct device_node *of_node; };
struct platform_device { struct device dev; };
struct resource { int unused; };
struct generic_pm_domain {
    const char *name;
    unsigned int flags;
    int (*power_on)(struct generic_pm_domain *);
    int (*power_off)(struct generic_pm_domain *);
};
struct genpd_onecell_data { struct generic_pm_domain **domains; int num_domains; };
struct of_device_id { const char *compatible; };
struct platform_driver {
    struct { const char *name; const struct of_device_id *of_match_table; bool suppress_bind_attrs; } driver;
    int (*probe)(struct platform_device *);
};
static u32 registers[128];
static struct resource resource;
static struct genpd_onecell_data allocation;
static int fail_alloc, fail_map, fail_init, fail_provider, fail_power;
static int writes, init_count, remove_count, provider_calls, registration_calls;
static unsigned int module_pins;
static int init_ids[5], removed_ids[5];
static bool init_off[5];
static const unsigned int status_offsets[] = {0x1c, 0x2c, 0x40, 0x80, 0x10c};
static const unsigned int enable_offsets[] = {0x18, 0x28, 0x3c, 0x7c, 0x108};
static const char *names[] = {"cpu1_domain", "ai_domain", "disp_domain", "vpu_domain", "dpu_domain"};
static int domain_id(struct generic_pm_domain *domain)
{
    int i;
    for (i = 0; i < 5; ++i) if (!strcmp(domain->name, names[i])) return i;
    assert(0); return -1;
}
static u32 readl(const void *address)
{
    ptrdiff_t index = (const u32 *)address - registers;
    assert(index >= 0 && index < 128);
    return registers[index];
}
static void writel(u32 value, void *address)
{
    unsigned int offset = (unsigned char *)address - (unsigned char *)registers;
    int i;
    assert(offset < sizeof(registers) && !(offset & 3));
    ++writes;
    registers[offset / 4] = value;
    for (i = 0; i < 5; ++i) {
        if (offset != enable_offsets[i] || i == fail_power) continue;
        if (value & BIT(1)) registers[status_offsets[i] / 4] = BIT(1);
        if (value & BIT(0)) registers[status_offsets[i] / 4] = BIT(0);
    }
}
static void udelay(unsigned int delay) { assert(delay == 1); }
static bool of_property_read_bool(struct device_node *node, const char *property)
{
    assert(!strcmp(property, "tdvp,cpu1-vision-domains")); return node->vision;
}
static void *devm_kzalloc(struct device *dev, size_t size, int flags)
{
    (void)dev; assert(size == sizeof(allocation) && !flags);
    memset(&allocation, 0, sizeof(allocation)); return fail_alloc ? NULL : &allocation;
}
static struct resource *platform_get_resource(struct platform_device *dev, int kind, int index)
{
    (void)dev; assert(!kind && !index); return &resource;
}
static void *devm_ioremap_resource(struct device *dev, struct resource *res)
{
    (void)dev; assert(res == &resource); return fail_map ? (void *)(intptr_t)-EIO : registers;
}
static int pm_genpd_init(struct generic_pm_domain *domain, void *governor, bool is_off)
{
    int id = domain_id(domain);
    assert(!governor && init_count < 5);
    if (id == fail_init) return -EINVAL;
    init_ids[init_count] = id; init_off[init_count++] = is_off; return 0;
}
static int pm_genpd_remove(struct generic_pm_domain *domain)
{
    assert(remove_count < 5); removed_ids[remove_count++] = domain_id(domain); return 0;
}
static int of_genpd_add_provider_onecell(struct device_node *node, struct genpd_onecell_data *data)
{
    (void)node; assert(data == &allocation && data->num_domains == 5);
    ++provider_calls; return fail_provider ? -EINVAL : 0;
}
static int dev_err_probe(struct device *dev, int error, const char *message)
{
    (void)dev; (void)message; assert(error < 0); return error;
}
static int platform_driver_register(struct platform_driver *driver)
{
    assert(driver->probe && driver->driver.of_match_table); ++registration_calls; return 0;
}

#include "drivers/soc/canaan/k230-power-domains.c"

static void reset_state(void)
{
    unsigned int i;
    memset(registers, 0, sizeof(registers));
    for (i = 0; i < 5; ++i) registers[status_offsets[i] / 4] = BIT(1);
    registers[0x160 / 4] = 7;
    fail_alloc = fail_map = fail_provider = 0;
    fail_init = fail_power = -1;
    writes = init_count = remove_count = provider_calls = 0;
    tdvp_power_owner = NULL; module_pins = 0;
    /* Intentionally retain static domain flags between probes. */
}
static void assert_cleanup(int initialized)
{
    int i;
    assert(init_count == initialized && remove_count == initialized);
    assert(!tdvp_power_owner && !module_pins);
    for (i = 0; i < initialized; ++i) assert(removed_ids[i] == initialized - 1 - i);
}
int main(void)
{
    struct device_node node = {.vision = true};
    struct platform_device device = {.dev = {.of_node = &node}};
    int i, variant;
    assert(!test_init_entry() && registration_calls == 1);
    assert(k230_power_domain_driver.driver.suppress_bind_attrs);
    for (variant = 0; variant < 3; ++variant) {
        reset_state(); node.vision = variant != 1;
        assert(!k230_power_domain_probe(&device));
        assert(k230_tdvp_power_ready(&node) == node.vision);
        assert(!k230_tdvp_power_ready(NULL));
        assert(module_pins == (unsigned int)node.vision);
        assert(init_count == 5 && provider_calls == 1 && !remove_count && !writes);
        for (i = 0; i < 5; ++i) {
            bool owned = node.vision && (i == K230_PM_DOMAIN_AI || i == K230_PM_DOMAIN_DISP);
            bool off = !owned && (i == K230_PM_DOMAIN_AI || i == K230_PM_DOMAIN_DISP || i == K230_PM_DOMAIN_VPU);
            assert(init_ids[i] == i && init_off[i] == off);
            if (i == K230_PM_DOMAIN_AI || i == K230_PM_DOMAIN_DISP) {
                assert(!!(k230_pm_domains[i]->flags & GENPD_FLAG_ALWAYS_ON) == owned);
                if (owned) assert(k230_pm_domains[i]->power_off(k230_pm_domains[i]) == -EBUSY);
            }
        }
        assert(!writes);
    }
    for (i = 0; i < 5; ++i) {
        reset_state(); fail_init = i;
        assert(k230_power_domain_probe(&device) == -EINVAL);
        assert_cleanup(i); assert(!provider_calls);
    }
    reset_state(); fail_provider = 1;
    assert(k230_power_domain_probe(&device) == -EINVAL);
    assert_cleanup(5); assert(provider_calls == 1);
    for (i = K230_PM_DOMAIN_AI; i <= K230_PM_DOMAIN_DISP; ++i) {
        reset_state(); fail_power = i; registers[status_offsets[i] / 4] = BIT(0);
        assert(k230_power_domain_probe(&device) == -EIO);
        assert_cleanup(i); assert(!provider_calls && writes > 0);
        reset_state(); registers[status_offsets[i] / 4] = BIT(0);
        assert(!k230_power_domain_probe(&device));
        assert(registers[status_offsets[i] / 4] & BIT(1));
    }
    reset_state(); fail_alloc = 1;
    assert(k230_power_domain_probe(&device) == -ENOMEM && !writes && !init_count);
    reset_state(); fail_map = 1;
    assert(k230_power_domain_probe(&device) == -EIO && !writes && !init_count);
    assert(!tdvp_power_owner && !module_pins);
    puts("CPU1 power AMP: PASS real patched probe, readiness only on success, live domains not cycled, off denied, failures unwound");
    return 0;
}
