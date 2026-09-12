// SPDX-License-Identifier: GPL-2.0
/* Fixed paired-image contract, not a generic remoteproc or physical-I/O API. */
#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/ktime.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/random.h>
#include "tdvp_cpu1_owner.h"
#include "tdvp_cpu1_owner_io.h"

/* 0070 supplies these only after the actual guarded providers initialize.
 * A kernel with just the DT flags (or only 0067-0069) cannot link/load us.
 */
extern bool k230_tdvp_gpio_ready(struct device_node *);
extern bool k230_tdvp_power_ready(struct device_node *);
extern bool k230_tdvp_clock_ready(struct device_node *);

/* Fixed K230 physical windows, outside this board's installed DDR. These are
 * Linux resource-tree claims on behalf of CPU1, NOT Linux SRAM allocations or
 * register mappings. The last 4 KiB covers GNNE (0x800), FFT (0x400), AI2D
 * (0x400). Shared SRAM must not be reused by a Linux decompressor/allocator.
 * Cooperative resource claims do not stop arbitrary MMIO/DMA by privileged
 * code and do not establish that boot-time decompression has quiesced.
 */
static const struct {
    resource_size_t base, bytes;
    const char *name;
} ai_resources[] = {
    {0x80000000, 0x200000, "tdvp-cpu1-kpu-sram"},
    {0x80200000, 0x200000, "tdvp-cpu1-shared-sram"},
    {0x80400000, 0x1000, "tdvp-cpu1-gnne-fft-ai2d"},
};

static int owner_reserved(struct device_node *node, const char *name,
                           phys_addr_t base, size_t bytes)
{
    struct device_node *memory;
    struct reserved_mem *registered;
    struct resource resource;
    int index, ret = -EINVAL;

    index = of_property_match_string(node, "memory-region-names", name);
    if (index < 0)
        return index;
    memory = of_parse_phandle(node, "memory-region", index);
    if (!memory)
        return -EINVAL;
    registered = of_reserved_mem_lookup(memory);
    if (of_device_is_available(memory) && of_property_read_bool(memory, "no-map") &&
        !of_property_read_bool(memory, "reusable") && registered &&
        !of_address_to_resource(memory, 0, &resource) &&
        resource.start == base && resource_size(&resource) == bytes &&
        registered->base == base && registered->size == bytes)
        ret = 0;
    of_node_put(memory);
    return ret;
}

static int owner_supplier(struct device_node *node, const char *property,
                           phys_addr_t base, bool (*ready)(struct device_node *))
{
    struct device_node *supplier = of_parse_phandle(node, property, 0);
    struct resource resource;
    int ret = -EINVAL;

    if (supplier && of_device_is_available(supplier) &&
        !of_address_to_resource(supplier, 0, &resource) && resource.start == base)
        ret = ready(supplier) ? 0 : -EPROBE_DEFER;
    of_node_put(supplier);
    return ret;
}

static int owner_declarations(struct device_node *node)
{
    static const char *const disabled[] = {
        "/soc/i2c@91409000", "/soc/i2c@91409000/gc2093@37", "/soc/isp.0",
        "/soc/mipi.0", "/soc/mipi.1", "/soc/mipi.2",
        "/soc/gnne@80400000", "/soc/ai2d@80400c00",
        "/soc/sysctl/sysctl_clock@91100000/i2c4_clk",
        "/soc/sysctl/sysctl_clock@91100000/i2c4_pclk_gate",
        "/soc/sysctl/sysctl_clock@91100000/tdvp_sensor_mclk1",
        "/soc/sysctl/sysctl_clock@91100000/tdvp_sensor_mclk1_mux",
        "/soc/sysctl/sysctl_clock@91100000/ai_clk",
        "/soc/sysctl/sysctl_clock@91100000/ai_aclk",
    };
    struct device_node *target;
    unsigned int i;

    if (of_count_phandle_with_args(node, "memory-region", NULL) != 2 ||
        of_property_count_strings(node, "memory-region-names") != 2 ||
        of_count_phandle_with_args(node, "clocks", "#clock-cells") != 5 ||
        of_property_count_strings(node, "clock-names") != 5 ||
        of_count_phandle_with_args(node, "power-domains", "#power-domain-cells") != 2 ||
        of_property_count_strings(node, "power-domain-names") != 2 ||
        of_find_property(node, "assigned-clocks", NULL))
        return -EINVAL;
    for (i = 0; i < ARRAY_SIZE(disabled); ++i) {
        target = of_find_node_by_path(disabled[i]);
        if (!target || of_device_is_available(target)) {
            of_node_put(target);
            return -EINVAL;
        }
        of_node_put(target);
    }
    return 0;
}

/* Validate named references before PM/clock acquisition, so a malformed DT
 * cannot make a supposedly harmless preparation touch another peripheral.
 */
static int owner_named_supplier(struct device_node *node, const char *list,
                                 const char *names, const char *cells,
                                 const char *name, const char *path, int id)
{
    struct of_phandle_args args;
    struct device_node *expected;
    int index, ret;

    index = of_property_match_string(node, names, name);
    if (index < 0)
        return -EINVAL;
    ret = of_parse_phandle_with_args(node, list, cells, index, &args);
    if (ret)
        return ret;
    expected = of_find_node_by_path(path);
    ret = expected && args.np == expected && of_device_is_available(expected) &&
          args.args_count == (id < 0 ? 0 : 1) && (id < 0 || args.args[0] == (u32)id) ? 0 : -EINVAL;
    of_node_put(expected);
    of_node_put(args.np);
    return ret;
}

static int owner_ddr_layout(struct device_node *node, unsigned int index, u32 bit)
{
    static const char *const properties[] = {
        "clk-gate-reg-offset", "clk-gate-reg-bit-enable", "clk-gate-reg-bit-reverse", "read-only"
    };
    const u32 expected[] = {0x60, bit, 0, 0};
    struct of_phandle_args args;
    struct device_node *parent, *pll;
    u32 value;
    unsigned int i;
    int ret = of_parse_phandle_with_args(node, "clocks", "#clock-cells", index, &args);
    if (ret) return ret;
    ret = -EINVAL;
    for (i = 0; i < ARRAY_SIZE(properties); ++i)
        if (of_property_read_u32(args.np, properties[i], &value) || value != expected[i]) goto out;
    if (of_find_property(args.np, "clk-rate-reg-offset", NULL) ||
        of_find_property(args.np, "clk-rate-reg-offset_1", NULL) ||
        of_find_property(args.np, "clk-parent-reg-offset", NULL) ||
        of_find_property(args.np, "assigned-clocks", NULL)) goto out;
    parent = of_parse_phandle(args.np, "clocks", 0);
    pll = of_find_node_by_path("/soc/sysctl/sysctl_boot@91102000/pll0_div4");
    if (parent && parent == pll) ret = 0;
    of_node_put(parent);
    of_node_put(pll);
out:
    of_node_put(args.np);
    return ret;
}

void tdvp_linux_owner_abort(struct tdvp_linux_owner *owner)
{
    unsigned int i;

    if (WARN_ON(owner->started))
        return;
    while (owner->exclusive)
        clk_rate_exclusive_put(owner->clocks[--owner->exclusive].clk);
    if (owner->clocks_enabled) {
        clk_bulk_disable_unprepare(ARRAY_SIZE(owner->clocks), owner->clocks);
        owner->clocks_enabled = false;
    }
    if (owner->clocks_acquired) {
        clk_bulk_put(ARRAY_SIZE(owner->clocks), owner->clocks);
        owner->clocks_acquired = false;
    }
    for (i = ARRAY_SIZE(owner->domains); i-- > 0;) {
        if (!owner->domains[i])
            continue;
        if (i < owner->powered)
            pm_runtime_put_sync(owner->domains[i]);
        dev_pm_domain_detach(owner->domains[i], true);
        owner->domains[i] = NULL;
    }
    owner->powered = 0;
    while (owner->ai_regions) {
        i = --owner->ai_regions;
        release_mem_region(ai_resources[i].base, ai_resources[i].bytes);
    }
}

int tdvp_linux_owner_prepare(struct device *dev, struct tdvp_linux_owner *owner)
{
    static const char *const clocks[] = {"pll0", "pll1", "pll2", "isp-ddr", "ai-ddr"};
    static const char *const paths[] = {
        "/soc/sysctl/sysctl_boot@91102000/pll0_div4",
        "/soc/sysctl/sysctl_boot@91102000/pll1_div4",
        "/soc/sysctl/sysctl_boot@91102000/pll2_div4",
        "/soc/sysctl/sysctl_clock@91100000/tdvp_isp_ddr",
        "/soc/sysctl/sysctl_clock@91100000/tdvp_ai_ddr",
    };
    static const char *const domains[] = {"ai", "disp"};
    static const unsigned long min_rates[] = {400000000, 594000000, 666000000, 400000000, 400000000};
    static const unsigned long max_rates[] = {400000000, 594000000, 666750000, 400000000, 400000000};
    struct device_node *node = dev->of_node;
    unsigned long rate;
    unsigned int i;
    int ret;

    ret = owner_declarations(node);
    if (ret)
        return ret;
    ret = owner_reserved(node, "transport", TDVP_VISION_SHARED_BASE, TDVP_VISION_SHARED_SIZE);
    if (!ret)
        ret = owner_reserved(node, "mmz", TDVP_VISION_MMZ_BASE, TDVP_VISION_MMZ_SIZE);
    if (!ret)
        ret = owner_supplier(node, "tdvp,gpio-controller", 0x9140b000, k230_tdvp_gpio_ready);
    if (!ret)
        ret = owner_supplier(node, "tdvp,power-controller", 0x91103000, k230_tdvp_power_ready);
    if (!ret)
        ret = owner_supplier(node, "tdvp,clock-controller", 0x91100000, k230_tdvp_clock_ready);
    if (ret)
        return ret;
    for (i = 0; i < ARRAY_SIZE(clocks); ++i) {
        ret = owner_named_supplier(node, "clocks", "clock-names", "#clock-cells",
                                   clocks[i], paths[i], -1);
        if (ret)
            return ret;
        owner->clocks[i].id = clocks[i];
        if (i >= 3) {
            int index = of_property_match_string(node, "clock-names", clocks[i]);
            ret = owner_ddr_layout(node, index, i == 3 ? 4 : 6);
            if (ret) return ret;
        }
    }
    for (i = 0; i < ARRAY_SIZE(domains); ++i) {
        ret = owner_named_supplier(node, "power-domains", "power-domain-names",
                                   "#power-domain-cells", domains[i],
                                   "/soc/sysctl/sysctl_power@91103000", i + 1);
        if (ret)
            return ret;
    }
    owner->control = devm_ioremap(dev, TDVP_OWNER_BASE, TDVP_OWNER_WINDOW);
    if (!owner->control)
        return -ENOMEM;
    for (i = 0; i < ARRAY_SIZE(ai_resources); ++i) {
        if (!request_mem_region(ai_resources[i].base, ai_resources[i].bytes,
                                ai_resources[i].name)) {
            ret = -EBUSY;
            goto failed;
        }
        ++owner->ai_regions;
    }
    for (i = 0; i < ARRAY_SIZE(domains); ++i) {
        struct device *domain = dev_pm_domain_attach_by_name(dev, domains[i]);

        if (IS_ERR_OR_NULL(domain)) {
            ret = domain ? PTR_ERR(domain) : -ENODEV;
            goto failed;
        }
        owner->domains[i] = domain;
        ret = pm_runtime_resume_and_get(domain);
        if (ret < 0)
            goto failed;
        ++owner->powered;
    }
    ret = clk_bulk_get(dev, ARRAY_SIZE(owner->clocks), owner->clocks);
    if (ret)
        goto failed;
    owner->clocks_acquired = true;
    /* Rate protection propagates to the shared PLL parents. Never retune them. */
    for (i = 0; i < ARRAY_SIZE(owner->clocks); ++i) {
        ret = clk_rate_exclusive_get(owner->clocks[i].clk);
        if (ret)
            goto failed;
        ++owner->exclusive;
        rate = clk_get_rate(owner->clocks[i].clk);
        if (rate < min_rates[i] || rate > max_rates[i]) {
            ret = -ERANGE;
            goto failed;
        }
    }
    ret = clk_bulk_prepare_enable(ARRAY_SIZE(owner->clocks), owner->clocks);
    if (ret)
        goto failed;
    owner->clocks_enabled = true;
    return 0;
failed:
    tdvp_linux_owner_abort(owner);
    return ret;
}

void tdvp_linux_owner_start(struct tdvp_linux_owner *owner)
{
    u64 cookie;

    if (WARN_ON(owner->started || !owner->clocks_enabled || owner->powered != 2 ||
                owner->ai_regions != ARRAY_SIZE(ai_resources)))
        return;
    do {
        cookie = get_random_u64();
    } while (!cookie);
    tdvp_owner_linux_init(&owner->session, cookie, ktime_to_ms(ktime_get()));
    owner->started = true;
    tdvp_linux_owner_publish(&owner->control->linux_side, &owner->session.own);
}

int tdvp_linux_owner_status(const struct tdvp_linux_owner *owner)
{
    if (!owner->started)
        return -EAGAIN;
    if (owner->session.own.state == TDVP_OWNER_FAULT)
        return owner->session.own.fault == TDVP_OWNER_ERR_TIMEOUT ? -ETIMEDOUT : -EIO;
    return owner->session.peer_ready ? 0 : -EAGAIN;
}

int tdvp_linux_owner_poll(struct tdvp_linux_owner *owner)
{
    struct tdvp_owner_record peer;
    bool stable;

    if (!owner->started)
        return -EAGAIN;
    stable = tdvp_linux_owner_snapshot(&owner->control->cpu1_side, &peer);
    tdvp_owner_linux_step(&owner->session, stable ? &peer : NULL, ktime_to_ms(ktime_get()));
    if (stable && peer.magic == TDVP_OWNER_MAGIC && peer.version == TDVP_OWNER_VERSION &&
        peer.bytes == sizeof(peer) && peer.contract == TDVP_OWNER_CONTRACT &&
        peer.cookie && peer.cookie == owner->session.own.peer_cookie &&
        peer.peer_cookie == owner->session.own.cookie)
        owner->last_peer = peer;
    tdvp_linux_owner_publish(&owner->control->linux_side, &owner->session.own);
    return tdvp_linux_owner_status(owner);
}
