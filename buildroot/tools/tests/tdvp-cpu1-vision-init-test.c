/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

extern int mpp_init(void);
extern int tdvp_cpu1_vision_init_status(void);
static int fail_at;
static int calls;

static int step(int expected)
{
    assert(++calls == expected);
    return calls == fail_at ? -100 - calls : 0;
}

int cmpi_init(void) { return step(1); }
int log_init(void) { return step(2); }
int mmz_init(unsigned long base, unsigned long size)
{
    assert(base == 0x14000000UL);
    assert(size == 0x08000000UL - 4096UL);
    return step(3);
}
int mmz_userdev_init(void) { return step(4); }
int sysctrl_init(void) { return step(5); }
int vb_init(void) { return step(6); }
int tdvp_cpu1_vision_pins_init(void) { return step(7); }
int vicap_init(void) { return step(8); }
int rt_kprintf(const char *format, ...) { (void)format; return 0; }

int main(int argc, char **argv)
{
    assert(argc == 2);
    fail_at = atoi(argv[1]);
    assert(fail_at >= 0 && fail_at <= 8);
    assert(tdvp_cpu1_vision_init_status() != 0);
    const int expected = fail_at ? -100 - fail_at : 0;
    assert(mpp_init() == expected);
    assert(tdvp_cpu1_vision_init_status() == expected);
    assert(calls == (fail_at ? fail_at : 8));
    /* Neither success nor partially initialized failure may run twice. */
    assert(mpp_init() == expected);
    assert(calls == (fail_at ? fail_at : 8));
    return 0;
}
