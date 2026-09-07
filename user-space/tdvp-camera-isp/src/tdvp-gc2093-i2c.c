/* SPDX-License-Identifier: MIT */
#define _XOPEN_SOURCE 700
#include "tdvp-gc2093-i2c.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define GC2093_ADDRESS 0x37

int tdvp_gc2093_read_reg(int fd, uint16_t reg, uint8_t *value)
{
    uint8_t address[] = { (uint8_t)(reg >> 8), (uint8_t)reg };
    struct i2c_msg messages[] = {
        { .addr = GC2093_ADDRESS, .flags = 0,
          .len = sizeof(address), .buf = address },
        { .addr = GC2093_ADDRESS, .flags = I2C_M_RD,
          .len = 1, .buf = value },
    };
    struct i2c_rdwr_ioctl_data transfer = { .msgs = messages, .nmsgs = 2 };

    if (fd < 0 || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    int result = ioctl(fd, I2C_RDWR, &transfer);
    if (result == 2)
        return 0;
    if (result >= 0)
        errno = EIO;
    return -1;
}

int tdvp_gc2093_open(void)
{
    char node[PATH_MAX];
    static const char controller[] = "/i2c@91409000";
    uint8_t high, low;

    /* Refuse a renumbered touch/dock bus even if /dev/i2c-4 exists. */
    if (realpath("/sys/class/i2c-dev/i2c-4/device/of_node", node) == NULL)
        return -1;
    size_t length = strlen(node);
    if (length < sizeof(controller) - 1 ||
        strcmp(node + length - (sizeof(controller) - 1), controller) != 0) {
        errno = ENODEV;
        return -1;
    }

    int fd = open("/dev/i2c-4", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -1;
    /* Respect a bound kernel client's ownership; never use SLAVE_FORCE. */
    if (ioctl(fd, I2C_SLAVE, (unsigned long)GC2093_ADDRESS) < 0 ||
        tdvp_gc2093_read_reg(fd, 0x03f0, &high) < 0 ||
        tdvp_gc2093_read_reg(fd, 0x03f1, &low) < 0)
        goto fail;
    if (high != 0x20 || low != 0x93) {
        errno = ENODEV;
        goto fail;
    }
    return fd;

fail: {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
}
