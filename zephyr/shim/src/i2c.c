/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/util.h>
#include <drivers/i2c.h>

#include "console.h"
#include "i2c.h"
#include "i2c/i2c.h"

/*
 * The named-i2c-ports node is required by the I2C shim
 */
#if !DT_NODE_EXISTS(DT_PATH(named_i2c_ports))
#error I2C shim requires the named-i2c-ports node to be defined.
#endif

/*
 * Initialize device bindings in i2c_devices.
 * This macro should be called from within DT_FOREACH_CHILD.
 */
#define INIT_DEV_BINDING(id) \
	[I2C_PORT(id)] = DEVICE_DT_GET(DT_PHANDLE(id, i2c_port)),

#define INIT_REMOTE_PORTS(id) \
	[I2C_PORT(id)] = DT_PROP_OR(id, remote_port, -1),

#define I2C_PORT_FLAGS(id)                                                     \
	COND_CODE_1(DT_PROP(id, dynamic_speed), (I2C_PORT_FLAG_DYNAMIC_SPEED), \
		    (0))

#define I2C_PORT_INIT(id)                    \
	{                                    \
		.name = DT_LABEL(id),        \
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
const struct i2c_port_t i2c_ports[] = {
	DT_FOREACH_CHILD(DT_PATH(named_i2c_ports), I2C_PORT_INIT)
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
static const int i2c_remote_ports[I2C_PORT_COUNT] = {
	DT_FOREACH_CHILD(DT_PATH(named_i2c_ports), INIT_REMOTE_PORTS)
};
static int i2c_physical_ports[I2C_PORT_COUNT];

static const struct device *i2c_devices[I2C_PORT_COUNT] = {
	DT_FOREACH_CHILD(DT_PATH(named_i2c_ports), INIT_DEV_BINDING)
};

static int init_device_bindings(const struct device *device)
{
	ARG_UNUSED(device);

	/*
	 * The EC application may lock the I2C bus for more than a single
	 * I2C transaction. Initialize the i2c_physical_ports[] array to map
	 * each named-i2c-ports child to the physical bus assignment.
	 */
	int i;
	int physical_port = 0;

	i2c_physical_ports[0] = physical_port;
	for (int child = 1; child < I2C_PORT_COUNT; child++) {
		for (i = 0; i < child; i++) {
			if (i2c_devices[child] == i2c_devices[i]) {
				i2c_physical_ports[child] =
					i2c_physical_ports[i];
				break;
			}
		}
		if (i == child)
			i2c_physical_ports[child] = ++physical_port;
	}
	__ASSERT(I2C_DEVICE_COUNT == 0 ||
			 physical_port == (I2C_DEVICE_COUNT - 1),
		 "I2C_DEVICE_COUNT is invalid");
	return 0;
}
SYS_INIT(init_device_bindings, POST_KERNEL, 51);

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

int i2c_get_physical_port(int enum_port)
{
	int i2c_port;

	if (enum_port < 0 || enum_port >= I2C_PORT_COUNT)
		return -1;

	i2c_port = i2c_physical_ports[enum_port];
	/*
	 * Return -1 for caller if physical port is not defined or the
	 * port number is out of port_mutex space.
	 * Please ensure the caller won't change anything if -1 received.
	 */
	return (i2c_port < I2C_DEVICE_COUNT) ? i2c_port : -1;
}

#ifdef CONFIG_PLATFORM_EC_CONSOLE_CMD_I2C_PORTMAP
static int command_i2c_portmap(int argc, char **argv)
{
	int i;

	ccprintf("Zephyr physical I2C ports (%d):\n", I2C_PORT_COUNT);
	for (i = 0; i < I2C_PORT_COUNT; i++) {
		ccprintf("  %d : %d\n", i, i2c_physical_ports[i]);
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
