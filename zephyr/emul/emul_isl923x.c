/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_isl923x_emul

#include <device.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>
#include <emul.h>
#include <errno.h>
#include <sys/__assert.h>

#include "driver/charger/isl923x.h"
#include "driver/charger/isl923x_public.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_isl923x.h"
#include "i2c.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(isl923x_emul, CONFIG_ISL923X_EMUL_LOG_LEVEL);

#define ISL923X_DATA_FROM_I2C_EMUL(_emul)                                    \
	CONTAINER_OF(CONTAINER_OF(_emul, struct i2c_common_emul_data, emul), \
		     struct isl923x_emul_data, common)

/** Mask used for the charge current register */
#define REG_CHG_CURRENT_MASK GENMASK(12, 2)

/** Mask used for the system voltage max register */
#define REG_SYS_VOLTAGE_MAX_MASK GENMASK(14, 3)

/** Mask used for the adapter current limit 1 register */
#define REG_ADAPTER_CURRENT_LIMIT1_MASK GENMASK(12, 2)

/** Mask used for the adapter current limit 2 register */
#define REG_ADAPTER_CURRENT_LIMIT2_MASK GENMASK(12, 2)

/** Mask used for the control 0 register */
#define REG_CONTROL0_MASK GENMASK(15, 1)

/** Mask used for the control 1 register */
#define REG_CONTROL1_MASK (GENMASK(15, 8) | GENMASK(6, 0))

struct isl923x_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;
	/** Emulated charge current limit register */
	uint16_t current_limit_reg;
	/** Emulated adapter current limit 1 register */
	uint16_t adapter_current_limit1_reg;
	/** Emulated adapter current limit 2 register */
	uint16_t adapter_current_limit2_reg;
	/** Emulated max voltage register */
	uint16_t max_volt_reg;
	/** Emulated manufacturer ID register */
	uint16_t manufacturer_id_reg;
	/** Emulated device ID register */
	uint16_t device_id_reg;
	/** Emulated control 0 register */
	uint16_t control_0_reg;
	/** Emulated control 1 register */
	uint16_t control_1_reg;
};

struct isl923x_emul_cfg {
	/** Common I2C config */
	const struct i2c_common_emul_cfg common;
};

struct i2c_emul *isl923x_emul_get_i2c_emul(const struct emul *emulator)
{
	struct isl923x_emul_data *data = emulator->data;

	return &(data->common.emul);
}

void isl923x_emul_set_manufacturer_id(const struct emul *emulator,
				      uint16_t manufacturer_id)
{
	struct isl923x_emul_data *data = emulator->data;

	data->manufacturer_id_reg = manufacturer_id;
}

void isl923x_emul_set_device_id(const struct emul *emulator,
				uint16_t device_id)
{
	struct isl923x_emul_data *data = emulator->data;

	data->device_id_reg = device_id;
}

bool isl923x_emul_is_learn_mode_enabled(const struct emul *emulator)
{
	struct isl923x_emul_data *data = emulator->data;

	return (data->control_1_reg & ISL923X_C1_LEARN_MODE_ENABLE) != 0;
}

void isl923x_emul_set_learn_mode_enabled(const struct emul *emulator,
					 bool enabled)
{
	struct isl923x_emul_data *data = emulator->data;

	if (enabled)
		data->control_1_reg |= ISL923X_C1_LEARN_MODE_ENABLE;
	else
		data->control_1_reg &= ~ISL923X_C1_LEARN_MODE_ENABLE;
}

static int isl923x_emul_read_byte(struct i2c_emul *emul, int reg, uint8_t *val,
				  int bytes)
{
	struct isl923x_emul_data *data = ISL923X_DATA_FROM_I2C_EMUL(emul);

	switch (reg) {
	case ISL923X_REG_CHG_CURRENT:
		__ASSERT_NO_MSG(bytes == 0 || bytes == 1);
		if (bytes == 0)
			*val = (uint8_t)(data->current_limit_reg & 0xff);
		else
			*val = (uint8_t)((data->current_limit_reg >> 8) & 0xff);
		break;
	case ISL923X_REG_SYS_VOLTAGE_MAX:
		__ASSERT_NO_MSG(bytes == 0 || bytes == 1);
		if (bytes == 0)
			*val = (uint8_t)(data->max_volt_reg & 0xff);
		else
			*val = (uint8_t)((data->max_volt_reg >> 8) & 0xff);
		break;
	case ISL923X_REG_ADAPTER_CURRENT_LIMIT1:
		__ASSERT_NO_MSG(bytes == 0 || bytes == 1);
		if (bytes == 0)
			*val = (uint8_t)(data->adapter_current_limit1_reg &
					 0xff);
		else
			*val = (uint8_t)((data->adapter_current_limit1_reg >>
					  8) &
					 0xff);
		break;
	case ISL923X_REG_ADAPTER_CURRENT_LIMIT2:
		__ASSERT_NO_MSG(bytes == 0 || bytes == 1);
		if (bytes == 0)
			*val = (uint8_t)(data->adapter_current_limit2_reg &
					 0xff);
		else
			*val = (uint8_t)((data->adapter_current_limit2_reg >>
					  8) &
					 0xff);
		break;
	case ISL923X_REG_MANUFACTURER_ID:
		__ASSERT_NO_MSG(bytes == 0 || bytes == 1);
		if (bytes == 0)
			*val = (uint8_t)(data->manufacturer_id_reg & 0xff);
		else
			*val = (uint8_t)((data->manufacturer_id_reg >> 8) &
					 0xff);
		break;
	case ISL923X_REG_DEVICE_ID:
		__ASSERT_NO_MSG(bytes == 0 || bytes == 1);
		if (bytes == 0)
			*val = (uint8_t)(data->device_id_reg & 0xff);
		else
			*val = (uint8_t)((data->device_id_reg >> 8) & 0xff);
		break;
	case ISL923X_REG_CONTROL0:
		__ASSERT_NO_MSG(bytes == 0 || bytes == 1);
		if (bytes == 0)
			*val = (uint8_t)(data->control_0_reg & 0xff);
		else
			*val = (uint8_t)((data->control_0_reg >> 8) & 0xff);
		break;
	case ISL923X_REG_CONTROL1:
		__ASSERT_NO_MSG(bytes == 0 || bytes == 1);
		if (bytes == 0)
			*val = (uint8_t)(data->control_1_reg & 0xff);
		else
			*val = (uint8_t)((data->control_1_reg >> 8) & 0xff);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int isl923x_emul_write_byte(struct i2c_emul *emul, int reg, uint8_t val,
				   int bytes)
{
	struct isl923x_emul_data *data = ISL923X_DATA_FROM_I2C_EMUL(emul);

	switch (reg) {
	case ISL923X_REG_CHG_CURRENT:
		__ASSERT_NO_MSG(bytes == 1 || bytes == 2);
		if (bytes == 1)
			data->current_limit_reg = val & REG_CHG_CURRENT_MASK;
		else
			data->current_limit_reg |= (val << 8) &
						   REG_CHG_CURRENT_MASK;
		break;
	case ISL923X_REG_SYS_VOLTAGE_MAX:
		__ASSERT_NO_MSG(bytes == 1 || bytes == 2);
		if (bytes == 1)
			data->max_volt_reg = val & REG_SYS_VOLTAGE_MAX_MASK;
		else
			data->max_volt_reg |= (val << 8) &
					      REG_SYS_VOLTAGE_MAX_MASK;
		break;
	case ISL923X_REG_ADAPTER_CURRENT_LIMIT1:
		__ASSERT_NO_MSG(bytes == 1 || bytes == 2);
		if (bytes == 1)
			data->adapter_current_limit1_reg =
				val & REG_ADAPTER_CURRENT_LIMIT1_MASK;
		else
			data->adapter_current_limit1_reg |=
				(val << 8) & REG_ADAPTER_CURRENT_LIMIT1_MASK;
		break;
	case ISL923X_REG_ADAPTER_CURRENT_LIMIT2:
		__ASSERT_NO_MSG(bytes == 1 || bytes == 2);
		if (bytes == 1)
			data->adapter_current_limit2_reg =
				val & REG_ADAPTER_CURRENT_LIMIT2_MASK;
		else
			data->adapter_current_limit2_reg |=
				(val << 8) & REG_ADAPTER_CURRENT_LIMIT2_MASK;
		break;
	case ISL923X_REG_CONTROL0:
		__ASSERT_NO_MSG(bytes == 1 || bytes == 2);
		if (bytes == 1)
			data->control_0_reg = val & REG_CONTROL0_MASK;
		else
			data->control_0_reg |= (val << 8) & REG_CONTROL0_MASK;
		break;
	case ISL923X_REG_CONTROL1:
		__ASSERT_NO_MSG(bytes == 1 || bytes == 2);
		if (bytes == 1)
			data->control_1_reg = val & REG_CONTROL1_MASK;
		else
			data->control_1_reg |= (val << 8) & REG_CONTROL1_MASK;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int emul_isl923x_init(const struct emul *emul,
			     const struct device *parent)
{
	const struct isl923x_emul_cfg *cfg = emul->cfg;
	struct isl923x_emul_data *data = emul->data;

	data->common.emul.api = &i2c_common_emul_api;
	data->common.emul.addr = cfg->common.addr;
	data->common.emul.parent = emul;
	data->common.i2c = parent;
	data->common.cfg = &cfg->common;
	i2c_common_emul_init(&data->common);

	return i2c_emul_register(parent, emul->dev_label, &data->common.emul);
}

#define INIT_ISL923X(n)                                                        \
	static struct isl923x_emul_data isl923x_emul_data_##n = {              \
		.common = {                                                    \
			.write_byte = isl923x_emul_write_byte,                 \
			.read_byte = isl923x_emul_read_byte,                   \
		},                                                             \
	};                                                                     \
	static struct isl923x_emul_cfg isl923x_emul_cfg_##n = {                \
	.common = {                                                            \
		.i2c_label = DT_INST_BUS_LABEL(n),                             \
		.dev_label = DT_INST_LABEL(n),                                 \
		.addr = DT_INST_REG_ADDR(n),                                   \
		},                                                             \
	};                                                                     \
	EMUL_DEFINE(emul_isl923x_init, DT_DRV_INST(n), &isl923x_emul_cfg_##n,  \
		    &isl923x_emul_data_##n)

DT_INST_FOREACH_STATUS_OKAY(INIT_ISL923X)
