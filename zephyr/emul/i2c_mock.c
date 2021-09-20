/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_i2c_mock

#include <device.h>
#include "emul/emul_common_i2c.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(i2c_mock, CONFIG_I2C_MOCK_LOG_LEVEL);

struct i2c_emul *i2c_mock_to_i2c_emul(const struct emul *emul)
{
	struct i2c_common_emul_data *data = emul->data;

	return &(data->emul);
}

void i2c_mock_reset(const struct emul *emul)
{
	struct i2c_emul *i2c_emul = i2c_mock_to_i2c_emul(emul);

	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_func(i2c_emul, NULL, NULL);
	i2c_common_emul_set_write_func(i2c_emul, NULL, NULL);
}

uint16_t i2c_mock_get_addr(const struct emul *emul)
{
	const struct i2c_common_emul_cfg *cfg = emul->cfg;

	return cfg->addr;
}

static const struct i2c_emul_api i2c_mock_api = {
	.transfer = i2c_common_emul_transfer,
};

static int i2c_mock_init(const struct emul *emul,
			 const struct device *parent)
{
	const struct i2c_common_emul_cfg *cfg = emul->cfg;
	struct i2c_common_emul_data *data = emul->data;

	data->emul.api = &i2c_mock_api;
	data->emul.addr = cfg->addr;
	data->emul.parent = emul;
	data->i2c = parent;
	data->cfg = cfg;
	i2c_common_emul_init(data);

	return i2c_emul_register(parent, emul->dev_label, &data->emul);
}

#define INIT_I2C_MOCK(n)                                              \
	static const struct i2c_common_emul_cfg i2c_mock_cfg_##n = {  \
		.i2c_label = DT_INST_BUS_LABEL(n),                    \
		.dev_label = DT_INST_LABEL(n),                        \
		.addr = DT_INST_REG_ADDR(n),                          \
	};                                                            \
	static struct i2c_common_emul_data i2c_mock_data_##n;         \
	EMUL_DEFINE(i2c_mock_init, DT_DRV_INST(n), &i2c_mock_cfg_##n, \
		    &i2c_mock_data_##n)

DT_INST_FOREACH_STATUS_OKAY(INIT_I2C_MOCK)
