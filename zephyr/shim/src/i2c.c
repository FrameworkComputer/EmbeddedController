/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/util.h>

#include "i2c.h"
#include "i2c/i2c.h"

/* Shorthand for getting the device binding for a given devicetree label. */
#define BINDING_FOR(label)                                                 \
	device_get_binding(                                                \
		COND_CODE_1(DT_NODE_HAS_STATUS(DT_NODELABEL(label), okay), \
			    (DT_LABEL(DT_NODELABEL(label))), ("")))

/*
 * Initialize device bindings in i2c_devices.
 * This macro should be called from within DT_FOREACH_CHILD.
 */
#define INIT_DEV_BINDING(id) \
	i2c_devices[I2C_PORT(id)] = BINDING_FOR(id);

/*
 * Long term we will not need these, for now they're needed to get things to
 * build since these extern symbols are usually defined in
 * board/${BOARD}/board.c.
 *
 * Since all the ports will eventually be handled by device tree. This will
 * be removed at that point.
 */
const struct i2c_port_t i2c_ports[] = {};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

int i2c_get_line_levels(int port)
{
	return I2C_LINE_IDLE;
}

static const struct device *i2c_devices[I2C_PORT_COUNT];

static int init_device_bindings(const struct device *device)
{
	ARG_UNUSED(device);
	DT_FOREACH_CHILD(DT_PATH(named_i2c_ports), INIT_DEV_BINDING)
	return 0;
}
SYS_INIT(init_device_bindings, POST_KERNEL, 51);

const struct device *i2c_get_device_for_port(const int port)
{
	if (port < 0 || port >= I2C_PORT_COUNT)
		return NULL;
	return i2c_devices[port];
}
