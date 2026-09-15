/* SPDX-License-Identifier: MIT */
#define _XOPEN_SOURCE 700
#include "tdvp-gc2093-i2c.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int scenario, opened, closed, selected, reads;
enum { OK, NO_NODE, WRONG_BUS, NO_DEVICE, BUSY, NACK_HIGH, NACK_LOW,
       SHORT_READ, WRONG_ID };

char *__wrap_realpath(const char *path, char *result)
{
    assert(strcmp(path, "/sys/class/i2c-dev/i2c-4/device/of_node") == 0);
    if (scenario == NO_NODE) {
        errno = ENOENT;
        return NULL;
    }
    strcpy(result, scenario == WRONG_BUS ?
           "/sys/firmware/devicetree/base/soc/i2c@91408000" :
           "/sys/firmware/devicetree/base/soc/i2c@91409000");
    return result;
}

/* Ubuntu's fortified libc redirects realpath to this checked entry point. */
char *__wrap___realpath_chk(const char *path, char *result, size_t length)
{
    assert(length == PATH_MAX);
    return __wrap_realpath(path, result);
}

int __wrap_open(const char *path, int flags, ...)
{
    assert(strcmp(path, "/dev/i2c-4") == 0);
    assert(flags == (O_RDWR | O_CLOEXEC));
    ++opened;
    if (scenario == NO_DEVICE) {
        errno = EACCES;
        return -1;
    }
    return 42;
}

int __wrap_close(int fd)
{
    assert(fd == 42);
    ++closed;
    errno = EBADF; /* Verify that cleanup preserves the original failure. */
    return 0;
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    assert(fd == 42);
    va_list args;
    va_start(args, request);
    if (request == I2C_SLAVE) {
        assert(va_arg(args, unsigned long) == 0x37);
        va_end(args);
        ++selected;
        if (scenario == BUSY) {
            errno = EBUSY;
            return -1;
        }
        return 0;
    }
    assert(request == I2C_RDWR); /* No FORCE, SMBUS or arbitrary scans. */
    struct i2c_rdwr_ioctl_data *transfer = va_arg(args, void *);
    va_end(args);
    assert(selected == 1 && transfer->nmsgs == 2);
    struct i2c_msg *messages = transfer->msgs;
    assert(messages[0].addr == 0x37 && messages[0].flags == 0);
    assert(messages[0].len == 2 && messages[0].buf[0] == 0x03);
    assert(messages[0].buf[1] == (reads ? 0xf1 : 0xf0));
    assert(messages[1].addr == 0x37 && messages[1].flags == I2C_M_RD);
    assert(messages[1].len == 1);
    ++reads;
    if ((scenario == NACK_HIGH && reads == 1) ||
        (scenario == NACK_LOW && reads == 2)) {
        errno = ENXIO;
        return -1;
    }
    if (scenario == SHORT_READ)
        return 1;
    *messages[1].buf = scenario == WRONG_ID ? 0 : (reads == 1 ? 0x20 : 0x93);
    return 2;
}

int main(void)
{
    const int errors[] = { 0, ENOENT, ENODEV, EACCES, EBUSY, ENXIO, ENXIO,
                           EIO, ENODEV };
    for (scenario = OK; scenario <= WRONG_ID; ++scenario) {
        opened = closed = selected = reads = 0;
        errno = 0;
        int fd = tdvp_gc2093_open();
        fprintf(stderr, "case=%d fd=%d errno=%d opened=%d closed=%d selected=%d reads=%d\n",
                scenario, fd, errno, opened, closed, selected, reads);
        if (scenario == OK) {
            assert(fd == 42 && opened == 1 && selected == 1 && reads == 2);
            assert(closed == 0);
            __wrap_close(fd);
        } else {
            assert(fd == -1 && errno == errors[scenario]);
            assert(opened == (scenario >= NO_DEVICE));
            assert(closed == (scenario > NO_DEVICE));
            if (scenario <= BUSY)
                assert(reads == 0);
            if (scenario == NACK_HIGH || scenario == SHORT_READ)
                assert(reads == 1);
        }
    }
    uint8_t value;
    assert(tdvp_gc2093_read_reg(-1, 0x03f0, &value) == -1 && errno == EINVAL);
    assert(tdvp_gc2093_read_reg(42, 0x03f0, NULL) == -1 && errno == EINVAL);
    puts("GC2093 I2C transport: PASS 9 cases; no sensor configuration writes");
    return 0;
}
