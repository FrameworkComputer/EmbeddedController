/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* HyperDebug I2C logic and console commands */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "util.h"

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{ .name = "I2C1",
	  .port = 0,
	  .kbps = 100,
	  .scl = GPIO_CN7_2,
	  .sda = GPIO_CN7_4 },
	{ .name = "I2C2",
	  .port = 1,
	  .kbps = 100,
	  .scl = GPIO_CN9_19,
	  .sda = GPIO_CN9_21 },
	{ .name = "I2C3",
	  .port = 2,
	  .kbps = 100,
	  .scl = GPIO_CN9_11,
	  .sda = GPIO_CN9_9 },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

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
	uint32_t bits_per_second = 100000;

	ccprintf("  %d %s %d bps\n", index, i2c_ports[index].name,
		 bits_per_second);

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

static int command_i2c(int argc, const char **argv)
{
	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	if (!strcasecmp(argv[1], "info"))
		return command_i2c_info(argc, argv);
	return EC_ERROR_PARAM1;
}
DECLARE_CONSOLE_COMMAND_FLAGS(i2c, command_i2c, "info [PORT]",
			      "I2C bus manipulation", CMD_FLAG_RESTRICTED);
