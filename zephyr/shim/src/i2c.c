/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/util.h>

#include "i2c.h"
#include "i2c/i2c.h"

/*
 * Initialize device bindings in i2c_devices.
 * This macro should be called from within DT_FOREACH_CHILD.
 */
#define INIT_DEV_BINDING(id) \
	i2c_devices[I2C_PORT(id)] = DEVICE_DT_GET(DT_PHANDLE(id, i2c_port));

#define INIT_REMOTE_PORTS(id) \
	i2c_remote_ports[I2C_PORT(id)] = DT_PROP_OR(id, remote_port, -1);

#define INIT_PHYSICAL_PORTS(id) \
	i2c_physical_ports[I2C_PORT(id)] = DT_PROP_OR(id, physical_port, -1);

#define I2C_CONFIG_GPIO(id, type) \
	DT_ENUM_UPPER_TOKEN(DT_CHILD(DT_CHILD(id, config), type), enum_name)

#define I2C_PORT_INIT(id)             \
	{                             \
		.name = DT_LABEL(id), \
		.port = I2C_PORT(id), \
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
#if DT_NODE_EXISTS(DT_PATH(named_i2c_ports))
	DT_FOREACH_CHILD(DT_PATH(named_i2c_ports), I2C_PORT_INIT)
#endif
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
static int i2c_remote_ports[I2C_PORT_COUNT];
static int i2c_physical_ports[I2C_PORT_COUNT];

int i2c_get_line_levels(int port)
{
	return I2C_LINE_IDLE;
}

static const struct device *i2c_devices[I2C_PORT_COUNT];

static int init_device_bindings(const struct device *device)
{
	ARG_UNUSED(device);
	DT_FOREACH_CHILD(DT_PATH(named_i2c_ports), INIT_DEV_BINDING)
	DT_FOREACH_CHILD(DT_PATH(named_i2c_ports), INIT_REMOTE_PORTS)
	DT_FOREACH_CHILD(DT_PATH(named_i2c_ports), INIT_PHYSICAL_PORTS)
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

	/* Remote port is not defined, return -1 to signal the problem */
	return -1;
}

int i2c_get_physical_port(int enum_port)
{
	int i2c_port = i2c_physical_ports[enum_port];

	/*
	 * Return -1 for caller if physical port is not defined or the
	 * port number is out of port_mutex space.
	 * Please ensure the caller won't change anything if -1 received.
	 */
	return (i2c_port < I2C_PORT_COUNT) ? i2c_port : -1;
}
