/* SPDX-License-Identifier: MIT */
#include "tdvp-gc2093-i2c.h"
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int fd = tdvp_gc2093_open();
    if (fd < 0) {
        perror("GC2093 chip ID: /dev/i2c-4 address 0x37");
        return 1;
    }
    close(fd);
    puts("GC2093 chip ID: PASS controller=91409000 bus=4 address=0x37 id=0x2093 (not a capture test)");
    return 0;
}
