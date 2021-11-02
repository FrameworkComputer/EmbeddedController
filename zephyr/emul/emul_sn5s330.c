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
	/** Emulated FUNC_SET2 register */
	uint8_t func_set2_reg;
	/** Emulated FUNC_SET3 register */
	uint8_t func_set3_reg;
	/** Emulated FUNC_SET4 register */
	uint8_t func_set4_reg;
	/** Emulated FUNC_SET5 register */
	uint8_t func_set5_reg;
	/** Emulated FUNC_SET6 register */
	uint8_t func_set6_reg;
	/** Emulated FUNC_SET7 register */
	uint8_t func_set7_reg;
	/** Emulated FUNC_SET8 register */
	uint8_t func_set8_reg;
	/** Emulated FUNC_SET9 register */
	uint8_t func_set9_reg;
	/** Emulated FUNC_SET10 register */
	uint8_t func_set10_reg;
	/** Emulated FUNC_SET11 register */
	uint8_t func_set11_reg;
	/** Emulated FUNC_SET12 register */
	uint8_t func_set12_reg;
	/** Emulated INT_STATUS_REG1 register */
	uint8_t int_status_reg1;
	/** Emulated INT_STATUS_REG2 register */
	uint8_t int_status_reg2;
	/** Emulated INT_STATUS_REG3 register */
	uint8_t int_status_reg3;
	/** Emulated INT_STATUS_REG4 register */
	/*
	 * TODO(b:205754232): Register name discrepancy
	 */
	uint8_t int_status_reg4;
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
	case SN5S330_FUNC_SET2:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->func_set2_reg;
		break;
	case SN5S330_FUNC_SET3:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->func_set3_reg;
		break;
	case SN5S330_FUNC_SET4:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->func_set4_reg;
		break;
	case SN5S330_FUNC_SET5:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->func_set5_reg;
		break;
	case SN5S330_FUNC_SET6:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->func_set6_reg;
		break;
	case SN5S330_FUNC_SET7:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->func_set7_reg;
		break;
	case SN5S330_FUNC_SET8:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->func_set8_reg;
		break;
	case SN5S330_FUNC_SET9:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->func_set9_reg;
		break;
	case SN5S330_FUNC_SET10:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->func_set10_reg;
		break;
	case SN5S330_FUNC_SET11:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->func_set11_reg;
		break;
	case SN5S330_FUNC_SET12:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->func_set12_reg;
		break;
	case SN5S330_INT_STATUS_REG1:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->int_status_reg1;
		break;
	case SN5S330_INT_STATUS_REG2:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->int_status_reg2;
		break;
	case SN5S330_INT_STATUS_REG3:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->int_status_reg3;
		break;
	case SN5S330_INT_STATUS_REG4:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->int_status_reg4;
		break;
	default:
		__ASSERT(false, "Unimplemented Register Access Error on 0x%x",
			 reg);
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
	case SN5S330_FUNC_SET2:
		__ASSERT_NO_MSG(bytes == 1);
		data->func_set2_reg = val;
		break;
	case SN5S330_FUNC_SET3:
		__ASSERT_NO_MSG(bytes == 1);
		data->func_set3_reg = val;
		break;
	case SN5S330_FUNC_SET4:
		__ASSERT_NO_MSG(bytes == 1);
		data->func_set4_reg = val;
		break;
	case SN5S330_FUNC_SET5:
		__ASSERT_NO_MSG(bytes == 1);
		data->func_set5_reg = val;
		break;
	case SN5S330_FUNC_SET6:
		__ASSERT_NO_MSG(bytes == 1);
		data->func_set6_reg = val;
		break;
	case SN5S330_FUNC_SET7:
		__ASSERT_NO_MSG(bytes == 1);
		data->func_set7_reg = val;
		break;
	case SN5S330_FUNC_SET8:
		__ASSERT_NO_MSG(bytes == 1);
		data->func_set8_reg = val;
		break;
	case SN5S330_FUNC_SET9:
		__ASSERT_NO_MSG(bytes == 1);
		data->func_set9_reg = val;
		break;
	case SN5S330_FUNC_SET10:
		__ASSERT_NO_MSG(bytes == 1);
		data->func_set10_reg = val;
		break;
	case SN5S330_FUNC_SET11:
		__ASSERT_NO_MSG(bytes == 1);
		data->func_set11_reg = val;
		break;
	case SN5S330_FUNC_SET12:
		__ASSERT_NO_MSG(bytes == 1);
		data->func_set12_reg = val;
		break;
	case SN5S330_INT_STATUS_REG1:
		/* fallthrough */
	case SN5S330_INT_STATUS_REG2:
		/* fallthrough */
	case SN5S330_INT_STATUS_REG3:
		__ASSERT(false,
			 "Write to an unverified-as-safe read-only register on "
			 "0x%x",
			 reg);
		/* fallthrough for checkpath */
	case SN5S330_INT_STATUS_REG4:
		__ASSERT_NO_MSG(bytes == 1);
		data->int_status_reg4 = val;
		break;
	default:
		__ASSERT(false, "Unimplemented Register Access Error on 0x%x",
			 reg);
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
