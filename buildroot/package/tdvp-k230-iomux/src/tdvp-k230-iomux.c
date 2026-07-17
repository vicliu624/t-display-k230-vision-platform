#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define TDVP_IOMUX_BASE 0x91105000UL
#define TDVP_IOMUX_SIZE 0x100UL
#define TDVP_IOMUX_PIN_COUNT 64U
#define TDVP_IOMUX_MSC_MASK 0x200U
#define TDVP_IOMUX_FUNC_SHIFT 11U

#define TDVP_CFG_GPIO 0x18fU
#define TDVP_CFG_I2C 0x18fU

struct tdvp_iomux {
	int mem_fd;
	volatile uint32_t *regs;
};

struct tdvp_pin_setting {
	unsigned int pin;
	unsigned int function;
	uint32_t default_cfg;
	const char *label;
};

static const struct tdvp_pin_setting tdvp_keyboard_pins[] = {
	/*
	 * LilyGO keyboard_ext.c pulses GPIO43, then sets IO46/IO47 to
	 * IIC4_SCL/IIC4_SDA before opening /dev/i2c4.
	 */
	{ 43, 0, TDVP_CFG_GPIO, "GPIO43 keyboard reset" },
	{ 46, 3, TDVP_CFG_I2C, "I2C4_SCL" },
	{ 47, 3, TDVP_CFG_I2C, "I2C4_SDA" },
};

static void tdvp_usage(const char *argv0)
{
	printf("usage: %s keyboard\n", argv0);
	printf("       %s dump [PIN...]\n", argv0);
	printf("\n");
	printf("Commands:\n");
	printf("  keyboard  configure IO43 as GPIO and IO46/IO47 as I2C4 for TCA8418 validation\n");
	printf("  dump      print raw K230 IOMUX registers; defaults to pins 43, 46, and 47\n");
}

static int tdvp_iomux_open(struct tdvp_iomux *iomux)
{
	iomux->mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (iomux->mem_fd < 0) {
		fprintf(stderr, "tdvp-k230-iomux: open /dev/mem failed: %s\n", strerror(errno));
		return -1;
	}

	iomux->regs = mmap(NULL, TDVP_IOMUX_SIZE, PROT_READ | PROT_WRITE,
			   MAP_SHARED, iomux->mem_fd, TDVP_IOMUX_BASE);
	if (iomux->regs == MAP_FAILED) {
		fprintf(stderr, "tdvp-k230-iomux: mmap 0x%lx failed: %s\n",
			TDVP_IOMUX_BASE, strerror(errno));
		close(iomux->mem_fd);
		iomux->mem_fd = -1;
		iomux->regs = NULL;
		return -1;
	}

	return 0;
}

static void tdvp_iomux_close(struct tdvp_iomux *iomux)
{
	if (iomux->regs && iomux->regs != MAP_FAILED)
		munmap((void *)iomux->regs, TDVP_IOMUX_SIZE);
	if (iomux->mem_fd >= 0)
		close(iomux->mem_fd);

	iomux->mem_fd = -1;
	iomux->regs = NULL;
}

static bool tdvp_pin_valid(unsigned long pin)
{
	return pin < TDVP_IOMUX_PIN_COUNT;
}

static uint32_t tdvp_read_pin(const struct tdvp_iomux *iomux, unsigned int pin)
{
	return iomux->regs[pin];
}

static uint32_t tdvp_build_value(uint32_t old_value, unsigned int function, uint32_t default_cfg)
{
	/*
	 * This mirrors LilyGO's RT-Smart fpioa helper:
	 *   register = (old_register & 0x200) | default_cfg | (io_sel << 11)
	 *
	 * Bit 9 is the voltage-control bit.  The vendor code preserves it
	 * instead of forcing a board-wide voltage assumption from userspace.
	 */
	return (old_value & TDVP_IOMUX_MSC_MASK) |
	       ((function & 0x7U) << TDVP_IOMUX_FUNC_SHIFT) |
	       default_cfg;
}

static void tdvp_dump_pin(const struct tdvp_iomux *iomux, unsigned int pin)
{
	uint32_t value = tdvp_read_pin(iomux, pin);

	printf("tdvp-k230-iomux: io%u raw=0x%08x io_sel=%u cfg=0x%03x di=%u\n",
	       pin, value, (value >> TDVP_IOMUX_FUNC_SHIFT) & 0x7U,
	       value & 0x1ffU, (value >> 31) & 0x1U);
}

static int tdvp_apply_setting(struct tdvp_iomux *iomux, const struct tdvp_pin_setting *setting)
{
	uint32_t old_value;
	uint32_t new_value;
	uint32_t readback;

	if (!tdvp_pin_valid(setting->pin)) {
		fprintf(stderr, "tdvp-k230-iomux: invalid pin %u\n", setting->pin);
		return -1;
	}

	old_value = tdvp_read_pin(iomux, setting->pin);
	new_value = tdvp_build_value(old_value, setting->function, setting->default_cfg);
	iomux->regs[setting->pin] = new_value;
	readback = tdvp_read_pin(iomux, setting->pin);

	printf("tdvp-k230-iomux: io%u %-24s old=0x%08x new=0x%08x read=0x%08x\n",
	       setting->pin, setting->label, old_value, new_value, readback);

	if ((readback & 0x3fffU) != (new_value & 0x3fffU)) {
		fprintf(stderr, "tdvp-k230-iomux: io%u readback mismatch\n", setting->pin);
		return -1;
	}

	return 0;
}

static int tdvp_cmd_keyboard(struct tdvp_iomux *iomux)
{
	int ret = 0;

	for (size_t i = 0; i < sizeof(tdvp_keyboard_pins) / sizeof(tdvp_keyboard_pins[0]); i++) {
		if (tdvp_apply_setting(iomux, &tdvp_keyboard_pins[i]) < 0)
			ret = -1;
	}

	return ret;
}

static int tdvp_parse_pin(const char *text, unsigned int *pin)
{
	char *end = NULL;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(text, &end, 0);
	if (errno || !end || *end != '\0' || !tdvp_pin_valid(parsed))
		return -1;

	*pin = (unsigned int)parsed;
	return 0;
}

static int tdvp_cmd_dump(struct tdvp_iomux *iomux, int argc, char **argv)
{
	unsigned int default_pins[] = { 43, 46, 47 };

	if (argc == 0) {
		for (size_t i = 0; i < sizeof(default_pins) / sizeof(default_pins[0]); i++)
			tdvp_dump_pin(iomux, default_pins[i]);
		return 0;
	}

	for (int i = 0; i < argc; i++) {
		unsigned int pin;

		if (tdvp_parse_pin(argv[i], &pin) < 0) {
			fprintf(stderr, "tdvp-k230-iomux: invalid pin: %s\n", argv[i]);
			return -1;
		}

		tdvp_dump_pin(iomux, pin);
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct tdvp_iomux iomux = {
		.mem_fd = -1,
		.regs = NULL,
	};
	int ret = 0;

	if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) {
		tdvp_usage(argv[0]);
		return argc < 2 ? 2 : 0;
	}

	if (tdvp_iomux_open(&iomux) < 0)
		return 1;

	if (!strcmp(argv[1], "keyboard")) {
		ret = tdvp_cmd_keyboard(&iomux);
	} else if (!strcmp(argv[1], "dump")) {
		ret = tdvp_cmd_dump(&iomux, argc - 2, &argv[2]);
	} else {
		tdvp_usage(argv[0]);
		ret = -1;
	}

	tdvp_iomux_close(&iomux);
	return ret < 0 ? 1 : 0;
}
