/* SPDX-License-Identifier: MIT */
#ifndef TDVP_GC2093_I2C_H
#define TDVP_GC2093_I2C_H

#include <stdint.h>

/* Board-only transport. No scanning, reset, mode setup or stream start. */
int tdvp_gc2093_open(void);
int tdvp_gc2093_read_reg(int fd, uint16_t reg, uint8_t *value);

#endif
