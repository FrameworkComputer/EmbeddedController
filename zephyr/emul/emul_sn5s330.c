/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_sn5s330_emul

#include <device.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>
#include <emul.h>
#include <errno.h>
#include <sys/__assert.h>

#include "driver/ppc/sn5s330.h"
#include "driver/ppc/sn5s330_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_sn5s330.h"
#include "i2c.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(sn5s330_emul, CONFIG_SN5S330_EMUL_LOG_LEVEL);

#define SN5S330_DATA_FROM_I2C_EMUL(_emul)                                    \
	CONTAINER_OF(CONTAINER_OF(_emul, struct i2c_common_emul_data, emul), \
		     struct sn5s330_emul_data, common)

struct sn5s330_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;
	/** Emulated FUNC_SET1 register */
	uint8_t func_set1_reg;
};

struct sn5s330_emul_cfg {
	/** Common I2C config */
	const struct i2c_common_emul_cfg common;
};

struct i2c_emul *sn5s330_emul_to_i2c_emul(const struct emul *emul)
{
	struct sn5s330_emul_data *data = emul->data;

	return &(data->common.emul);
}

int sn5s330_emul_peek_reg(const struct emul *emul, uint32_t reg, uint32_t *val)
{
	struct sn5s330_emul_data *data = emul->data;

	switch (reg) {
	case SN5S330_FUNC_SET1:
		*val = data->func_set1_reg;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sn5s330_emul_read_byte(struct i2c_emul *emul, int reg, uint8_t *val,
				  int bytes)
{
	struct sn5s330_emul_data *data = SN5S330_DATA_FROM_I2C_EMUL(emul);

	switch (reg) {
	case SN5S330_FUNC_SET1:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->func_set1_reg;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sn5s330_emul_write_byte(struct i2c_emul *emul, int reg, uint8_t val,
				   int bytes)
{
	struct sn5s330_emul_data *data = SN5S330_DATA_FROM_I2C_EMUL(emul);

	switch (reg) {
	case SN5S330_FUNC_SET1:
		__ASSERT_NO_MSG(bytes == 1);
		data->func_set1_reg = val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int emul_sn5s330_init(const struct emul *emul,
			     const struct device *parent)
{
	const struct sn5s330_emul_cfg *cfg = emul->cfg;
	struct sn5s330_emul_data *data = emul->data;

	data->common.emul.api = &i2c_common_emul_api;
	data->common.emul.addr = cfg->common.addr;
	data->common.emul.parent = emul;
	data->common.i2c = parent;
	data->common.cfg = &cfg->common;
	i2c_common_emul_init(&data->common);

	return i2c_emul_register(parent, emul->dev_label, &data->common.emul);
}

#define INIT_SN5S330(n)                                                        \
	static struct sn5s330_emul_data sn5s330_emul_data_##n = {              \
		.common = {                                                    \
			.write_byte = sn5s330_emul_write_byte,                 \
			.read_byte = sn5s330_emul_read_byte,                   \
		},                                                             \
	};                                                                     \
	static struct sn5s330_emul_cfg sn5s330_emul_cfg_##n = {                \
		.common = {                                                    \
			.i2c_label = DT_INST_BUS_LABEL(n),                     \
			.dev_label = DT_INST_LABEL(n),                         \
			.addr = DT_INST_REG_ADDR(n),                           \
		},                                                             \
	};                                                                     \
	EMUL_DEFINE(emul_sn5s330_init, DT_DRV_INST(n), &sn5s330_emul_cfg_##n,  \
		    &sn5s330_emul_data_##n)

DT_INST_FOREACH_STATUS_OKAY(INIT_SN5S330)
