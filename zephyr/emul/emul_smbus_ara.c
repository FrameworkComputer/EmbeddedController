/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_ZTEST
#include <zephyr/ztest.h>
#endif

#define DT_DRV_COMPAT zephyr_smbus_ara_emul

struct smbus_ara_emul_data {
	struct i2c_common_emul_data common;
	uint8_t device_address[CONFIG_USB_PD_PORT_MAX_COUNT];
	uint8_t addr_used_map;
};

BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT < 8,
	     "Too many ports to represent with u8 bitmap");

int emul_smbus_ara_queue_address(const struct emul *emul, int port,
				 uint8_t address)
{
	struct smbus_ara_emul_data *data = emul->data;

	if (port >= CONFIG_USB_PD_PORT_MAX_COUNT) {
		return -EINVAL;
	}

	data->addr_used_map |= (1 << port);
	data->device_address[port] = address;

	return 0;
}

static int smbus_ara_emul_start_read(const struct emul *emul, int reg)
{
	return 0;
}

static int smbus_ara_emul_read_byte(const struct emul *emul, int reg,
				    uint8_t *val, int bytes)
{
	struct smbus_ara_emul_data *data = emul->data;
	uint8_t min_addr = 0;

	/* Return lowest port stored address for ARA. */
	if (data->addr_used_map) {
		for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; ++i) {
			if (data->addr_used_map & (1 << i)) {
				min_addr = data->device_address[i];
				data->addr_used_map &= ~(1 << i);
				break;
			}
		}
	}

	*val = min_addr << 1;
	return 0;
}

static int smbus_ara_emul_finish_read(const struct emul *emul, int reg,
				      int bytes)
{
	return 0;
}

static int smbus_ara_emul_access_reg(const struct emul *emul, int reg,
				     int bytes, bool read)
{
	return reg;
}

static int smbus_ara_emul_init(const struct emul *emul,
			       const struct device *parent)
{
	struct smbus_ara_emul_data *data = emul->data;
	const struct i2c_common_emul_cfg *cfg = emul->cfg;

	data->common.i2c = parent;
	data->common.cfg = cfg;

	data->addr_used_map = 0;

	i2c_common_emul_init(&data->common);

	return 0;
}

#define SMBUS_ARA_EMUL_DEFINE(n)                                              \
	static struct smbus_ara_emul_data smbus_ara_emul_data_##n = {	\
		.common = {						\
			.start_read = smbus_ara_emul_start_read,	\
			.read_byte = smbus_ara_emul_read_byte,		\
			.finish_read = smbus_ara_emul_finish_read,	\
			.access_reg = smbus_ara_emul_access_reg,	\
		},							\
	};       \
	static const struct i2c_common_emul_cfg smbus_ara_emul_cfg_##n = {    \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),               \
		.data = &smbus_ara_emul_data_##n.common,                      \
		.addr = DT_INST_REG_ADDR(n),                                  \
	};                                                                    \
	EMUL_DT_INST_DEFINE(n, smbus_ara_emul_init, &smbus_ara_emul_data_##n, \
			    &smbus_ara_emul_cfg_##n, &i2c_common_emul_api,    \
			    NULL)

DT_INST_FOREACH_STATUS_OKAY(SMBUS_ARA_EMUL_DEFINE)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE)
