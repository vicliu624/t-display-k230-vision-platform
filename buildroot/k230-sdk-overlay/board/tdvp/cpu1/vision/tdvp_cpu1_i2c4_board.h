/* SPDX-License-Identifier: MIT */
/* Included in the pinned drv_i2c.c, after its private types and operations. */
#if !defined(RT_USING_I2C4) || defined(RT_USING_I2C4_SLAVE) || \
    defined(RT_USING_I2C0) || defined(RT_USING_I2C1) || \
    defined(RT_USING_I2C2) || defined(RT_USING_I2C3)
#error "CPU1 vision must own only I2C4, in master mode"
#endif
_Static_assert(IC_CLK == 100, "early clock and DesignWare timing disagree");
_Static_assert(sizeof(i2c_buses) / sizeof(i2c_buses[0]) == 1,
               "CPU1 vision must not register Linux I2C controllers");

extern int tdvp_cpu1_i2c4_clock_prepare(void);
static int tdvp_i2c4_attempted;
static int tdvp_i2c4_status = -RT_EBUSY;

int tdvp_cpu1_i2c4_init_status(void)
{
    return tdvp_i2c4_status;
}

static int tdvp_cpu1_i2c4_board_init(void)
{
    struct chip_i2c_bus *bus = &i2c_buses[0];
    int result;

    if (tdvp_i2c4_attempted) return tdvp_i2c4_status;
    tdvp_i2c4_attempted = 1;
    if ((uintptr_t)bus->i2c.regs != 0x91409000UL || bus->slave) {
        result = -RT_EINVAL;
        goto done;
    }
    result = tdvp_cpu1_i2c4_clock_prepare();
    if (result) goto done;
    bus->i2c.regs = rt_ioremap((void *)0x91409000UL, 0x10000);
    if (!bus->i2c.regs) { result = -RT_ENOMEM; goto done; }
    bus->parent.ops = &chip_i2c_ops;
    bus->clock = 100000000U;
    dw_i2c_init(bus->i2c.regs);
    result = designware_i2c_set_bus_speed(bus, I2C_FAST_SPEED);
    if (result) goto done;
    if (!(readl(&bus->i2c.regs->ic_enable_status) & 1U)) {
        result = -RT_EIO;
        goto done;
    }
    result = rt_i2c_bus_device_register(&bus->parent, bus->device_name);
done:
    /* BOARD init failures are not fatal to RT-Thread itself. MPP must check
     * this latched status before touching media hardware. No retry/reset or
     * shared clock rollback after a partially completed initialization.
     */
    tdvp_i2c4_status = result;
    if (result) rt_kprintf("TDVP CPU1: I2C4 board init failed: %d\n", result);
    return result;
}
