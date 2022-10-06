/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_i2c_mock

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"

LOG_MODULE_REGISTER(i2c_mock, CONFIG_I2C_MOCK_LOG_LEVEL);

struct i2c_emul *i2c_mock_to_i2c_emul(const struct emul *emul)
{
	struct i2c_common_emul_data *data = emul->data;

	return &(data->emul);
}

void i2c_mock_reset(const struct emul *emul)
{
	i2c_common_emul_set_read_fail_reg(emul->data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(emul->data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_func(emul->data, NULL, NULL);
	i2c_common_emul_set_write_func(emul->data, NULL, NULL);
}

uint16_t i2c_mock_get_addr(const struct emul *emul)
{
	const struct i2c_common_emul_cfg *cfg = emul->cfg;

	return cfg->addr;
}

static const struct i2c_emul_api i2c_mock_api = {
	.transfer = i2c_common_emul_transfer,
};

static int i2c_mock_init(const struct emul *emul, const struct device *parent)
{
	const struct i2c_common_emul_cfg *cfg = emul->cfg;
	struct i2c_common_emul_data *data = emul->data;

	data->emul.api = &i2c_mock_api;
	data->emul.addr = cfg->addr;
	data->emul.target = emul;
	data->i2c = parent;
	data->cfg = cfg;
	i2c_common_emul_init(data);

	return 0;
}

#define INIT_I2C_MOCK(n)                                             \
	static const struct i2c_common_emul_cfg i2c_mock_cfg_##n = { \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),      \
		.addr = DT_INST_REG_ADDR(n),                         \
	};                                                           \
	static struct i2c_common_emul_data i2c_mock_data_##n;        \
	EMUL_DT_INST_DEFINE(n, i2c_mock_init, &i2c_mock_data_##n,    \
			    &i2c_mock_cfg_##n, &i2c_common_emul_api)

DT_INST_FOREACH_STATUS_OKAY(INIT_I2C_MOCK)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

struct i2c_common_emul_data *
emul_i2c_mock_get_i2c_common_data(const struct emul *emul)
{
	return emul->data;
}
