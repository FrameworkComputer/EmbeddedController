/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/device.h>

#include "basic_i2c_device_emul.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"

#define DT_DRV_COMPAT basic_i2c_device

void basic_i2c_device_reset(const struct emul *emul)
{
	struct basic_i2c_device_data *data =
		(struct basic_i2c_device_data *)emul->data;

	memset(data->regs, 0, sizeof(data->regs));
	memset(data->extended_regs, 0, sizeof(data->extended_regs));
}

static int basic_i2c_device_write(const struct emul *emul, int reg, uint8_t val,
				  int bytes, void *unused_data)
{
	struct basic_i2c_device_data *data =
		(struct basic_i2c_device_data *)emul->data;

	uint8_t *regs;

	if (data->regs[BASIC_I2C_DEV_EXT_ACCESS_REG]) {
		/* Accessing the extended register set */
		regs = data->extended_regs;

		/* Decrement one to account for the extended access reg byte */
		reg = data->regs[BASIC_I2C_DEV_EXT_ACCESS_REG] - 1;
	} else {
		regs = data->regs;
	}

	int pos = reg + bytes - 1;

	if (!IN_RANGE(pos, 0, sizeof(data->regs) - 1)) {
		return -1;
	}
	regs[pos] = val;

	return 0;
}

static int basic_i2c_device_read(const struct emul *emul, int reg, uint8_t *val,
				 int bytes, void *unused_data)
{
	struct basic_i2c_device_data *data =
		(struct basic_i2c_device_data *)emul->data;

	uint8_t *regs;

	if (data->regs[BASIC_I2C_DEV_EXT_ACCESS_REG]) {
		/* Accessing the extended register set */
		regs = data->extended_regs;
		reg = data->regs[BASIC_I2C_DEV_EXT_ACCESS_REG];
	} else {
		regs = data->regs;
	}

	int pos = reg + bytes;

	if (!IN_RANGE(pos, 0, sizeof(data->regs) - 1)) {
		return -1;
	}
	*val = regs[pos];

	return 0;
}

static int basic_i2c_device_init(const struct emul *emul,
				 const struct device *parent)
{
	struct basic_i2c_device_data *data =
		(struct basic_i2c_device_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, basic_i2c_device_read, NULL);
	i2c_common_emul_set_write_func(common_data, basic_i2c_device_write,
				       NULL);

	basic_i2c_device_reset(emul);

	return 0;
}

#define INIT_BASIC_I2C_DEVICE_EMUL(n)                                     \
	static struct i2c_common_emul_cfg common_cfg_##n;                 \
	static struct basic_i2c_device_data basic_i2c_device_data_##n;    \
	static struct i2c_common_emul_cfg common_cfg_##n = {              \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),           \
		.data = &basic_i2c_device_data_##n.common,                \
		.addr = DT_INST_REG_ADDR(n)                               \
	};                                                                \
	static struct basic_i2c_device_data basic_i2c_device_data_##n = { \
		.common = { .cfg = &common_cfg_##n }                      \
	};                                                                \
	EMUL_DT_INST_DEFINE(n, basic_i2c_device_init,                     \
			    &basic_i2c_device_data_##n, &common_cfg_##n,  \
			    &i2c_common_emul_api)

DT_INST_FOREACH_STATUS_OKAY(INIT_BASIC_I2C_DEVICE_EMUL);

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
