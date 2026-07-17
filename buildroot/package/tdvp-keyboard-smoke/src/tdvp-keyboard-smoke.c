#include <errno.h>
#include <fcntl.h>
#include <gpiod.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define TDVP_DEFAULT_ADDR 0x34
#define TDVP_DEFAULT_SECONDS 10
#define TDVP_DEFAULT_RESET_GPIO 43
#define TDVP_MAX_I2C_BUSES 8
#define TDVP_RESET_PULSE_US 10000

#define TCA8418_REG_CFG 0x01
#define TCA8418_REG_INT_STAT 0x02
#define TCA8418_REG_KEY_LCK_EC 0x03
#define TCA8418_REG_KEY_EVENT_A 0x04
#define TCA8418_REG_GPIO_INT_EN1 0x1a
#define TCA8418_REG_GPIO_INT_EN2 0x1b
#define TCA8418_REG_GPIO_INT_EN3 0x1c
#define TCA8418_REG_KP_GPIO1 0x1d
#define TCA8418_REG_KP_GPIO2 0x1e
#define TCA8418_REG_KP_GPIO3 0x1f
#define TCA8418_REG_GPI_EM1 0x20
#define TCA8418_REG_GPI_EM2 0x21
#define TCA8418_REG_GPI_EM3 0x22
#define TCA8418_REG_GPIO_DIR1 0x23
#define TCA8418_REG_GPIO_DIR2 0x24
#define TCA8418_REG_GPIO_DIR3 0x25
#define TCA8418_REG_GPIO_INT_LVL1 0x26
#define TCA8418_REG_GPIO_INT_LVL2 0x27
#define TCA8418_REG_GPIO_INT_LVL3 0x28

struct tdvp_key_name {
	int code;
	const char *name;
};

static const char *tdvp_key_names[70] = {
	"RIGHT", "LEFT", "FN", "RESERVED", "SPACE", "TAB", "SHIFT", "META", "FN", "CAPSLOCK",
	"RESERVED", "DOWN", "M", "SPACE", "V", "C", "X", "Z", "RESERVED", "Q",
	"ENTER", "UP", "RESERVED", "N", "B", "F", "D", "S", "A", "RESERVED",
	"RESERVED", "L", "K", "J", "H", "G", "R", "E", "W", "ESC",
	"BACKSPACE", "P", "O", "I", "U", "Y", "T", "2", "1", "RESERVED",
	"0", "9", "8", "7", "6", "5", "4", "3", "RESERVED", "RESERVED",
	"RESERVED", "RESERVED", "RESERVED", "RESERVED", "RESERVED", "RESERVED", "RESERVED", "RESERVED", "RESERVED", "RESERVED",
};

static void tdvp_usage(const char *argv0)
{
	printf("usage: %s [--bus /dev/i2c-N] [--addr 0x34] [--seconds N] [--no-init]\n", argv0);
	printf("       [--reset-gpio N] [--no-reset]\n");
	printf("\n");
	printf("If --bus is omitted, /dev/i2c-0..7 are scanned for a TCA8418 at the selected address.\n");
	printf("By default, GPIO43 is pulsed high/low/high before probing, matching the LilyGO demo.\n");
}

static long tdvp_parse_long(const char *value, const char *name)
{
	char *end = NULL;
	long parsed;

	errno = 0;
	parsed = strtol(value, &end, 0);
	if (errno || !end || *end != '\0') {
		fprintf(stderr, "tdvp-keyboard-smoke: invalid %s: %s\n", name, value);
		exit(2);
	}

	return parsed;
}

static int tdvp_i2c_read_reg(int fd, uint8_t addr, uint8_t reg, uint8_t *value)
{
	struct i2c_msg msgs[2];
	struct i2c_rdwr_ioctl_data data;

	msgs[0].addr = addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &reg;

	msgs[1].addr = addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = value;

	data.msgs = msgs;
	data.nmsgs = 2;

	if (ioctl(fd, I2C_RDWR, &data) < 0)
		return -1;

	return 0;
}

static int tdvp_i2c_write_reg(int fd, uint8_t addr, uint8_t reg, uint8_t value)
{
	uint8_t buffer[2] = { reg, value };
	struct i2c_msg msg;
	struct i2c_rdwr_ioctl_data data;

	msg.addr = addr;
	msg.flags = 0;
	msg.len = sizeof(buffer);
	msg.buf = buffer;

	data.msgs = &msg;
	data.nmsgs = 1;

	if (ioctl(fd, I2C_RDWR, &data) < 0)
		return -1;

	return 0;
}

static int tdvp_keyboard_init(int fd, uint8_t addr)
{
	uint8_t cfg = 0;

	if (tdvp_i2c_write_reg(fd, addr, TCA8418_REG_GPIO_DIR1, 0x00) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_GPIO_DIR2, 0x00) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_GPIO_DIR3, 0x00) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_GPI_EM1, 0xff) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_GPI_EM2, 0xff) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_GPI_EM3, 0xff) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_GPIO_INT_LVL1, 0x00) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_GPIO_INT_LVL2, 0x00) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_GPIO_INT_LVL3, 0x00) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_GPIO_INT_EN1, 0xff) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_GPIO_INT_EN2, 0xff) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_GPIO_INT_EN3, 0xff) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_KP_GPIO1, 0x7f) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_KP_GPIO2, 0xff) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_KP_GPIO3, 0x03) < 0)
		return -1;

	if (tdvp_i2c_read_reg(fd, addr, TCA8418_REG_CFG, &cfg) < 0)
		return -1;

	cfg = (uint8_t)((cfg & 0xf0) | 0x01);

	if (tdvp_i2c_write_reg(fd, addr, TCA8418_REG_CFG, cfg) < 0 ||
	    tdvp_i2c_write_reg(fd, addr, TCA8418_REG_INT_STAT, 0x01) < 0)
		return -1;

	return 0;
}

static int tdvp_pulse_reset_line(unsigned int gpio, unsigned int chip_index, unsigned int line_offset)
{
	char chip_path[32];
	struct gpiod_chip *chip;
	struct gpiod_line *line;
	int ret = -1;

	snprintf(chip_path, sizeof(chip_path), "/dev/gpiochip%u", chip_index);

	chip = gpiod_chip_open(chip_path);
	if (!chip) {
		fprintf(stderr, "tdvp-keyboard-smoke: open %s for GPIO%u reset failed: %s\n",
			chip_path, gpio, strerror(errno));
		return -1;
	}

	line = gpiod_chip_get_line(chip, line_offset);
	if (!line) {
		fprintf(stderr, "tdvp-keyboard-smoke: get GPIO%u line %u failed: %s\n",
			gpio, line_offset, strerror(errno));
		goto out_close_chip;
	}

	if (gpiod_line_request_output(line, "tdvp-keyboard-smoke", 1) < 0) {
		fprintf(stderr, "tdvp-keyboard-smoke: request GPIO%u output failed: %s\n",
			gpio, strerror(errno));
		goto out_close_chip;
	}

	usleep(TDVP_RESET_PULSE_US);
	if (gpiod_line_set_value(line, 0) < 0)
		goto out_release_line;

	usleep(TDVP_RESET_PULSE_US);
	if (gpiod_line_set_value(line, 1) < 0)
		goto out_release_line;

	usleep(TDVP_RESET_PULSE_US);
	printf("tdvp-keyboard-smoke: pulsed reset GPIO%u via %s line %u\n",
	       gpio, chip_path, line_offset);
	ret = 0;

out_release_line:
	if (ret < 0)
		fprintf(stderr, "tdvp-keyboard-smoke: pulse GPIO%u failed: %s\n",
			gpio, strerror(errno));
	gpiod_line_release(line);
out_close_chip:
	gpiod_chip_close(chip);
	return ret;
}

static int tdvp_pulse_reset_gpio(unsigned int gpio)
{
	if (gpio >= 32 && tdvp_pulse_reset_line(gpio, gpio / 32, gpio % 32) == 0)
		return 0;

	if (tdvp_pulse_reset_line(gpio, 0, gpio) == 0)
		return 0;

	return -1;
}

static double tdvp_now_seconds(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static const char *tdvp_key_name(unsigned int code_index)
{
	if (code_index == 0 || code_index > 70)
		return "GPIO/UNKNOWN";

	return tdvp_key_names[code_index - 1];
}

static int tdvp_open_bus(const char *bus)
{
	int fd = open(bus, O_RDWR);

	if (fd < 0)
		fprintf(stderr, "tdvp-keyboard-smoke: open %s failed: %s\n", bus, strerror(errno));

	return fd;
}

static int tdvp_probe_bus(const char *bus, uint8_t addr, bool init)
{
	int fd;
	uint8_t value = 0;

	fd = tdvp_open_bus(bus);
	if (fd < 0)
		return -1;

	if (tdvp_i2c_read_reg(fd, addr, TCA8418_REG_KEY_LCK_EC, &value) < 0) {
		close(fd);
		return -1;
	}

	if (init && tdvp_keyboard_init(fd, addr) < 0) {
		fprintf(stderr, "tdvp-keyboard-smoke: %s detected addr 0x%02x but init failed: %s\n",
			bus, addr, strerror(errno));
		close(fd);
		return -1;
	}

	printf("tdvp-keyboard-smoke: detected TCA8418-compatible keyboard on %s addr 0x%02x\n",
	       bus, addr);

	return fd;
}

static int tdvp_find_bus(uint8_t addr, bool init, char *selected, size_t selected_len)
{
	char bus[32];
	int fd;

	for (int i = 0; i < TDVP_MAX_I2C_BUSES; i++) {
		snprintf(bus, sizeof(bus), "/dev/i2c-%d", i);
		fd = tdvp_probe_bus(bus, addr, init);
		if (fd >= 0) {
			snprintf(selected, selected_len, "%s", bus);
			return fd;
		}
	}

	return -1;
}

static int tdvp_poll_events(int fd, uint8_t addr, int seconds)
{
	double end = tdvp_now_seconds() + seconds;
	unsigned int events = 0;

	printf("tdvp-keyboard-smoke: polling for %d seconds; press keys now\n", seconds);

	while (tdvp_now_seconds() < end) {
		uint8_t int_stat = 0;
		uint8_t lock_ec = 0;
		unsigned int count;

		if (tdvp_i2c_read_reg(fd, addr, TCA8418_REG_INT_STAT, &int_stat) < 0) {
			fprintf(stderr, "tdvp-keyboard-smoke: read INT_STAT failed: %s\n", strerror(errno));
			return 1;
		}

		if (!(int_stat & 0x01)) {
			usleep(20000);
			continue;
		}

		if (tdvp_i2c_read_reg(fd, addr, TCA8418_REG_KEY_LCK_EC, &lock_ec) < 0) {
			fprintf(stderr, "tdvp-keyboard-smoke: read KEY_LCK_EC failed: %s\n", strerror(errno));
			return 1;
		}

		count = lock_ec & 0x0f;
		for (unsigned int i = 0; i < count; i++) {
			uint8_t event = 0;
			unsigned int pressed;
			unsigned int code_index;
			unsigned int row;
			unsigned int col;

			if (tdvp_i2c_read_reg(fd, addr, TCA8418_REG_KEY_EVENT_A, &event) < 0) {
				fprintf(stderr, "tdvp-keyboard-smoke: read KEY_EVENT_A failed: %s\n",
					strerror(errno));
				return 1;
			}

			pressed = (event >> 7) & 0x01;
			code_index = event & 0x7f;
			row = code_index ? ((code_index - 1) / 10) : 0;
			col = code_index ? ((code_index - 1) % 10) : 0;
			events++;

			printf("tdvp-keyboard-smoke: key %-7s code=%u row=%u col=%u name=%s\n",
			       pressed ? "press" : "release", code_index, row, col,
			       tdvp_key_name(code_index));
		}

		if (tdvp_i2c_write_reg(fd, addr, TCA8418_REG_INT_STAT, 0x01) < 0) {
			fprintf(stderr, "tdvp-keyboard-smoke: clear INT_STAT failed: %s\n", strerror(errno));
			return 1;
		}
	}

	printf("tdvp-keyboard-smoke: PASS device detected, events=%u\n", events);
	return 0;
}

int main(int argc, char **argv)
{
	const char *bus = NULL;
	char selected_bus[32] = { 0 };
	uint8_t addr = TDVP_DEFAULT_ADDR;
	int seconds = TDVP_DEFAULT_SECONDS;
	unsigned int reset_gpio = TDVP_DEFAULT_RESET_GPIO;
	bool init = true;
	bool reset = true;
	int fd;
	int ret;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			tdvp_usage(argv[0]);
			return 0;
		} else if (!strcmp(argv[i], "--bus") && i + 1 < argc) {
			bus = argv[++i];
		} else if (!strcmp(argv[i], "--addr") && i + 1 < argc) {
			long parsed = tdvp_parse_long(argv[++i], "addr");
			if (parsed < 0 || parsed > 0x7f) {
				fprintf(stderr, "tdvp-keyboard-smoke: addr out of range\n");
				return 2;
			}
			addr = (uint8_t)parsed;
		} else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
			long parsed = tdvp_parse_long(argv[++i], "seconds");
			if (parsed < 0 || parsed > 3600) {
				fprintf(stderr, "tdvp-keyboard-smoke: seconds out of range\n");
				return 2;
			}
			seconds = (int)parsed;
		} else if (!strcmp(argv[i], "--no-init")) {
			init = false;
		} else if (!strcmp(argv[i], "--reset-gpio") && i + 1 < argc) {
			long parsed = tdvp_parse_long(argv[++i], "reset-gpio");
			if (parsed < 0 || parsed > 4095) {
				fprintf(stderr, "tdvp-keyboard-smoke: reset-gpio out of range\n");
				return 2;
			}
			reset_gpio = (unsigned int)parsed;
		} else if (!strcmp(argv[i], "--no-reset")) {
			reset = false;
		} else {
			tdvp_usage(argv[0]);
			return 2;
		}
	}

	if (reset && tdvp_pulse_reset_gpio(reset_gpio) < 0)
		fprintf(stderr, "tdvp-keyboard-smoke: continuing without confirmed reset\n");

	if (bus) {
		fd = tdvp_probe_bus(bus, addr, init);
		snprintf(selected_bus, sizeof(selected_bus), "%s", bus);
	} else {
		fd = tdvp_find_bus(addr, init, selected_bus, sizeof(selected_bus));
	}

	if (fd < 0) {
		fprintf(stderr, "tdvp-keyboard-smoke: no TCA8418-compatible device found at addr 0x%02x\n",
			addr);
		return 1;
	}

	printf("tdvp-keyboard-smoke: using %s\n", selected_bus[0] ? selected_bus : bus);
	ret = tdvp_poll_events(fd, addr, seconds);
	close(fd);

	return ret;
}
