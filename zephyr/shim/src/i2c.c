/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "i2c.h"
#include "i2c/i2c.h"

#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/util.h>

/*
 * Initialize device bindings in i2c_devices.
 * This macro should be called from within DT_FOREACH_CHILD.
 */
#define INIT_DEV_BINDING(i2c_port_id) \
	[I2C_PORT_BUS(i2c_port_id)] = DEVICE_DT_GET(i2c_port_id),

#define INIT_REMOTE_PORTS(id) [I2C_PORT(id)] = DT_PROP_OR(id, remote_port, -1),

#define I2C_PORT_FLAGS(id)                                                     \
	COND_CODE_1(DT_PROP(id, dynamic_speed), (I2C_PORT_FLAG_DYNAMIC_SPEED), \
		    (0))

#define I2C_PORT_INIT(id)                    \
	{                                    \
		.port = I2C_PORT(id),        \
		.flags = I2C_PORT_FLAGS(id), \
	},
/*
 * Long term we will not need these, for now they're needed to get things to
 * build since these extern symbols are usually defined in
 * board/${BOARD}/board.c.
 *
 * Since all the ports will eventually be handled by device tree. This will
 * be removed at that point.
 */
const struct i2c_port_t i2c_ports[] = { DT_FOREACH_CHILD_STATUS_OKAY(
	NAMED_I2C_PORTS_NODE, I2C_PORT_INIT) };
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
static const int i2c_remote_ports[I2C_PORT_COUNT] = {
	DT_FOREACH_CHILD_STATUS_OKAY(NAMED_I2C_PORTS_NODE, INIT_REMOTE_PORTS)
};

static const struct device *i2c_devices[I2C_PORT_COUNT] = { I2C_FOREACH_PORT(
	INIT_DEV_BINDING) };

const struct device *i2c_get_device_for_port(const int port)
{
	if (port < 0 || port >= I2C_PORT_COUNT)
		return NULL;
	return i2c_devices[port];
}

int i2c_get_port_from_remote_port(int remote_port)
{
	for (int port = 0; port < I2C_PORT_COUNT; port++) {
		if (i2c_remote_ports[port] == remote_port)
			return port;
	}

	/*
	 * Remote port is not defined, return 1:1 mapping to support TCPC
	 * firmware updates, which always query the EC for the correct I2C
	 * port number.
	 */
	return remote_port;
}

#ifdef CONFIG_PLATFORM_EC_CONSOLE_CMD_I2C_PORTMAP
static int command_i2c_portmap(int argc, const char **argv)
{
	int i;

	if (argc > 1) {
		return EC_ERROR_PARAM_COUNT;
	}

	ccprintf("Zephyr remote I2C ports (%d):\n", I2C_PORT_COUNT);
	for (i = 0; i < I2C_PORT_COUNT; i++) {
		ccprintf("  %d : %d\n", i, i2c_remote_ports[i]);
	}

	return EC_RES_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2c_portmap, command_i2c_portmap, NULL,
			"Show I2C port mapping");
#endif /* CONFIG_PLATFORM_EC_CONSOLE_CMD_I2C_PORTMAP */

int chip_i2c_set_freq(int port, enum i2c_freq freq)
{
	uint32_t dev_config;
	uint32_t speed;
	int ret = EC_SUCCESS;

	switch (freq) {
	case I2C_FREQ_100KHZ:
		speed = I2C_SPEED_STANDARD;
		break;
	case I2C_FREQ_400KHZ:
		speed = I2C_SPEED_FAST;
		break;
	case I2C_FREQ_1000KHZ:
		speed = I2C_SPEED_FAST_PLUS;
		break;
	default:
		return EC_ERROR_INVAL;
	}

	ret = i2c_get_config(i2c_get_device_for_port(port), &dev_config);
	if (!ret) {
		dev_config &= ~I2C_SPEED_MASK;
		dev_config |= I2C_SPEED_SET(speed);
		ret = i2c_configure(i2c_get_device_for_port(port), dev_config);
	}

	return ret;
}

enum i2c_freq chip_i2c_get_freq(int port)
{
	uint32_t dev_config;
	int ret = EC_SUCCESS;
	const struct device *dev = i2c_get_device_for_port(port);

	if (dev == NULL)
		return I2C_FREQ_COUNT;

	ret = i2c_get_config(dev, &dev_config);

	if (ret)
		return I2C_FREQ_COUNT;

	switch (I2C_SPEED_GET(dev_config)) {
	case I2C_SPEED_STANDARD:
		return I2C_FREQ_100KHZ;
	case I2C_SPEED_FAST:
		return I2C_FREQ_400KHZ;
	case I2C_SPEED_FAST_PLUS:
		return I2C_FREQ_1000KHZ;
	default:
		return I2C_FREQ_COUNT;
	}
}

enum i2c_ports i2c_get_port_from_device(const struct device *i2c_dev)
{
	for (int i = 0; i < I2C_PORT_COUNT; i++) {
		if (i2c_devices[i] == i2c_dev) {
			return i;
		}
	}
	return -1;
}
