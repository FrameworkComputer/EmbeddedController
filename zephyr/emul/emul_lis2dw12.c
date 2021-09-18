/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_lis2dw12_emul

#include <device.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>
#include <emul.h>
#include <errno.h>
#include <sys/__assert.h>

#include "driver/accel_lis2dw12.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_lis2dw12.h"
#include "i2c.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(lis2dw12_emul, CONFIG_LIS2DW12_EMUL_LOG_LEVEL);

#define LIS2DW12_DATA_FROM_I2C_EMUL(_emul)                                   \
	CONTAINER_OF(CONTAINER_OF(_emul, struct i2c_common_emul_data, emul), \
		     struct lis2dw12_emul_data, common)

struct lis2dw12_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;
	/** Emulated who-am-i register */
	uint8_t who_am_i_reg;
	/** Emulated ctrl2 register */
	uint8_t ctrl2_reg;
};

struct lis2dw12_emul_cfg {
	/** Common I2C config */
	struct i2c_common_emul_cfg common;
};

struct i2c_emul *lis2dw12_emul_to_i2c_emul(const struct emul *emul)
{
	struct lis2dw12_emul_data *data = emul->data;

	return &(data->common.emul);
}

void lis2dw12_emul_reset(const struct emul *emul)
{
	struct lis2dw12_emul_data *data = emul->data;

	i2c_common_emul_set_read_fail_reg(lis2dw12_emul_to_i2c_emul(emul),
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(lis2dw12_emul_to_i2c_emul(emul),
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	data->who_am_i_reg = LIS2DW12_WHO_AM_I;
	data->ctrl2_reg = 0;
}

void lis2dw12_emul_set_who_am_i(const struct emul *emul, uint8_t who_am_i)
{
	struct lis2dw12_emul_data *data = emul->data;

	data->who_am_i_reg = who_am_i;
}

static int lis2dw12_emul_read_byte(struct i2c_emul *emul, int reg, uint8_t *val,
				   int bytes)
{
	struct lis2dw12_emul_data *data = LIS2DW12_DATA_FROM_I2C_EMUL(emul);

	switch (reg) {
	case LIS2DW12_WHO_AM_I_REG:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->who_am_i_reg;
		break;
	case LIS2DW12_CTRL2_ADDR:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->ctrl2_reg;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int lis2dw12_emul_write_byte(struct i2c_emul *emul, int reg, uint8_t val,
				    int bytes)
{
	struct lis2dw12_emul_data *data = LIS2DW12_DATA_FROM_I2C_EMUL(emul);

	switch (reg) {
	case LIS2DW12_WHO_AM_I_REG:
		LOG_ERR("Can't write to who-am-i register");
		return -EINVAL;
	case LIS2DW12_CTRL2_ADDR:
		__ASSERT_NO_MSG(bytes == 1);
		data->ctrl2_reg = val;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static struct i2c_emul_api lis2dw12_emul_api_i2c = {
	.transfer = i2c_common_emul_transfer,
};

static int emul_lis2dw12_init(const struct emul *emul,
			      const struct device *parent)
{
	const struct lis2dw12_emul_cfg *lis2dw12_cfg = emul->cfg;
	const struct i2c_common_emul_cfg *cfg = &(lis2dw12_cfg->common);
	struct lis2dw12_emul_data *data = emul->data;

	data->common.emul.api = &lis2dw12_emul_api_i2c;
	data->common.emul.addr = cfg->addr;
	data->common.emul.parent = emul;
	data->common.i2c = parent;
	data->common.cfg = cfg;
	i2c_common_emul_init(&data->common);

	return i2c_emul_register(parent, emul->dev_label, &data->common.emul);
}

#define INIT_LIS2DW12(n)                                                  \
	static struct lis2dw12_emul_data lis2dw12_emul_data_##n = {       \
		.common = {                                               \
			.write_byte = lis2dw12_emul_write_byte,           \
			.read_byte = lis2dw12_emul_read_byte,             \
		},                                                        \
	};                                                                \
	static const struct lis2dw12_emul_cfg lis2dw12_emul_cfg_##n = {   \
		.common = {                                               \
			.i2c_label = DT_INST_BUS_LABEL(n),                \
			.dev_label = DT_INST_LABEL(n),                    \
			.addr = DT_INST_REG_ADDR(n),                      \
		},                                                        \
	};                                                                \
	EMUL_DEFINE(emul_lis2dw12_init, DT_DRV_INST(n),                   \
		    &lis2dw12_emul_cfg_##n, &lis2dw12_emul_data_##n)

DT_INST_FOREACH_STATUS_OKAY(INIT_LIS2DW12)
