/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* HyperDebug I2C logic and console commands */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "registers.h"
#include "util.h"

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "I2C1",
	  .port = 0,
	  .kbps = 100,
	  .scl = GPIO_CN7_2,
	  .sda = GPIO_CN7_4,
	  .flags = I2C_PORT_FLAG_DYNAMIC_SPEED },
	{ .name = "I2C2",
	  .port = 1,
	  .kbps = 100,
	  .scl = GPIO_CN9_19,
	  .sda = GPIO_CN9_21,
	  .flags = I2C_PORT_FLAG_DYNAMIC_SPEED },
	{ .name = "I2C3",
	  .port = 2,
	  .kbps = 100,
	  .scl = GPIO_CN9_11,
	  .sda = GPIO_CN9_9,
	  .flags = I2C_PORT_FLAG_DYNAMIC_SPEED },
	{ .name = "I2C4",
	  .port = 3,
	  .kbps = 100,
	  .scl = GPIO_CN10_8,
	  .sda = GPIO_CN10_12,
	  .flags = I2C_PORT_FLAG_DYNAMIC_SPEED },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

static uint32_t i2c_ports_bits_per_second[ARRAY_SIZE(i2c_ports)];

int usb_i2c_board_is_enabled(void)
{
	return 1;
}

/*
 * Find i2c port by name or by number.  Returns an index into i2c_ports[], or on
 * error a negative value.
 */
static int find_i2c_by_name(const char *name)
{
	int i;
	char *e;
	i = strtoi(name, &e, 0);

	if (!*e && i < i2c_ports_used)
		return i;

	for (i = 0; i < i2c_ports_used; i++) {
		if (!strcasecmp(name, i2c_ports[i].name))
			return i;
	}

	/* I2C device not found */
	return -1;
}

static void print_i2c_info(int index)
{
	ccprintf("  %d %s %d bps\n", index, i2c_ports[index].name,
		 i2c_ports_bits_per_second[index]);

	/* Flush console to avoid truncating output */
	cflush();
}

/*
 * Get information about one or all I2C ports.
 */
static int command_i2c_info(int argc, const char **argv)
{
	int i;

	/* If a I2C port is specified, print only that one */
	if (argc == 3) {
		int index = find_i2c_by_name(argv[2]);
		if (index < 0) {
			ccprintf("I2C port not found\n");
			return EC_ERROR_PARAM2;
		}

		print_i2c_info(index);
		return EC_SUCCESS;
	}

	/* Otherwise print them all */
	for (i = 0; i < i2c_ports_used; i++) {
		print_i2c_info(i);
	}

	return EC_SUCCESS;
}

/*
 * Constants copied from i2c-stm32l4.c, for 16MHz base frequency.
 */
static const uint32_t TIMINGR_I2C_FREQ_1000KHZ = 0x00000107;
static const uint32_t TIMINGR_I2C_FREQ_400KHZ = 0x00100B15;
static const uint32_t TIMINGR_I2C_FREQ_100KHZ = 0x00303D5B;

/*
 * This function builds on a similar function in i2c-stm32l4.c, adding support
 * for non-standard speeds slower than 100kbps.
 */
static void board_i2c_set_speed(int port, uint32_t desired_speed)
{
	i2c_lock(port, 1);

	/* Disable port */
	STM32_I2C_CR1(port) = 0;

	if (desired_speed >= 1000000) {
		/* Set clock frequency */
		STM32_I2C_TIMINGR(port) = TIMINGR_I2C_FREQ_1000KHZ;
		i2c_ports_bits_per_second[port] = 1000000;
	} else if (desired_speed >= 400000) {
		STM32_I2C_TIMINGR(port) = TIMINGR_I2C_FREQ_400KHZ;
		i2c_ports_bits_per_second[port] = 400000;
	} else {
		/*
		 * The code below uses the above constant meant for 100kbps I2C
		 * clock, and possibly modifies the prescaling value, to divide
		 * the frequency with an integer in the range 1-16.  It will
		 * find the closest I2C frequency in the range 6.25kbps -
		 * 100kbps which is not faster than the requested speed, except
		 * if the requested speed is slower than the slowest supported
		 * value.
		 */
		int divisor = 100000 / (desired_speed + 1);
		if (divisor > 15)
			divisor = 15;
		STM32_I2C_TIMINGR(port) = TIMINGR_I2C_FREQ_100KHZ |
					  (divisor << 28);
		i2c_ports_bits_per_second[port] = 100000 / (divisor + 1);
	}

	/* Enable port */
	STM32_I2C_CR1(port) = STM32_I2C_CR1_PE;

	i2c_lock(port, 0);
}

static int command_i2c_set_speed(int argc, const char **argv)
{
	int index;
	uint32_t desired_speed;
	char *e;
	if (argc < 5)
		return EC_ERROR_PARAM_COUNT;

	index = find_i2c_by_name(argv[3]);
	if (index < 0)
		return EC_ERROR_PARAM3;

	desired_speed = strtoi(argv[4], &e, 0);
	if (*e)
		return EC_ERROR_PARAM4;

	board_i2c_set_speed(index, desired_speed);

	return EC_SUCCESS;
}

static int command_i2c_set(int argc, const char **argv)
{
	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[2], "speed"))
		return command_i2c_set_speed(argc, argv);
	return EC_ERROR_PARAM2;
}

static int command_i2c(int argc, const char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[1], "info"))
		return command_i2c_info(argc, argv);
	if (!strcasecmp(argv[1], "set"))
		return command_i2c_set(argc, argv);
	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND_FLAGS(i2c, command_i2c,
			      "info [PORT]"
			      "\nset speed PORT BPS",
			      "I2C bus manipulation", CMD_FLAG_RESTRICTED);

/* Reconfigure I2C ports to power-on default values. */
static void i2c_reinit(void)
{
	for (unsigned int i = 0; i < i2c_ports_used; i++) {
		board_i2c_set_speed(i, i2c_ports[i].kbps * 1000);
		i2c_ports_bits_per_second[i] = i2c_ports[i].kbps * 1000;
	}
}
DECLARE_HOOK(HOOK_REINIT, i2c_reinit, HOOK_PRIO_DEFAULT);

static void board_i2c_init(void)
{
	for (unsigned int i = 0; i < i2c_ports_used; i++) {
		i2c_ports_bits_per_second[i] = i2c_ports[i].kbps * 1000;
	}
}
DECLARE_HOOK(HOOK_INIT, board_i2c_init, HOOK_PRIO_DEFAULT + 2);
