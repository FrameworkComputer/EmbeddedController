/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/util.h>

#include "registers.h"
#include "i2c/i2c.h"

/* Shorthand for getting the device binding for a given devicetree label. */
#define BINDING_FOR(label)                                                 \
	device_get_binding(                                                \
		COND_CODE_1(DT_NODE_HAS_STATUS(DT_NODELABEL(label), okay), \
			    (DT_LABEL(DT_NODELABEL(label))), ("")))

static const struct device *i2c_devices[NPCX_I2C_COUNT];

static int init_device_bindings(const struct device *device)
{
	ARG_UNUSED(device);
	i2c_devices[NPCX_I2C_PORT0_0] = BINDING_FOR(i2c0_0);
	i2c_devices[NPCX_I2C_PORT1_0] = BINDING_FOR(i2c1_0);
	i2c_devices[NPCX_I2C_PORT2_0] = BINDING_FOR(i2c2_0);
	i2c_devices[NPCX_I2C_PORT3_0] = BINDING_FOR(i2c3_0);
#ifdef CHIP_VARIANT_NPCX7M6G
	i2c_devices[NPCX_I2C_PORT4_0] = BINDING_FOR(i2c4_0);
#endif
	i2c_devices[NPCX_I2C_PORT4_1] = BINDING_FOR(i2c4_1);
	i2c_devices[NPCX_I2C_PORT5_0] = BINDING_FOR(i2c5_0);
	i2c_devices[NPCX_I2C_PORT5_1] = BINDING_FOR(i2c5_1);
	i2c_devices[NPCX_I2C_PORT6_0] = BINDING_FOR(i2c6_0);
	i2c_devices[NPCX_I2C_PORT6_1] = BINDING_FOR(i2c6_1);
	i2c_devices[NPCX_I2C_PORT7_0] = BINDING_FOR(i2c7_0);
	return 0;
}
SYS_INIT(init_device_bindings, POST_KERNEL, 51);

const struct device *i2c_get_device_for_port(const int port)
{
	if (port < 0 || port >= NPCX_I2C_COUNT)
		return NULL;
	return i2c_devices[port];
}
