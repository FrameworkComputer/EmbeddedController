/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_lis2dw12_emul

#include <device.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>
#include <drivers/emul.h>
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
	/** Emulated ctrl1 register */
	uint8_t ctrl1_reg;
	/** Emulated ctrl2 register */
	uint8_t ctrl2_reg;
	/** Emulated ctrl3 register */
	uint8_t ctrl3_reg;
	/** Emulated ctrl6 register */
	uint8_t ctrl6_reg;
	/** Emulated status register */
	uint8_t status_reg;
	/** Soft reset count */
	uint32_t soft_reset_count;
	/** Current X, Y, and Z output data registers */
	int16_t accel_data[3];
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
	struct i2c_emul *i2c_emul = lis2dw12_emul_to_i2c_emul(emul);

	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_func(i2c_emul, NULL, NULL);
	i2c_common_emul_set_write_func(i2c_emul, NULL, NULL);
	data->who_am_i_reg = LIS2DW12_WHO_AM_I;
	data->ctrl1_reg = 0;
	data->ctrl2_reg = 0;
	data->ctrl3_reg = 0;
	data->ctrl6_reg = 0;
	data->status_reg = 0;
	data->soft_reset_count = 0;

	memset(data->accel_data, 0, sizeof(data->accel_data));
}

void lis2dw12_emul_set_who_am_i(const struct emul *emul, uint8_t who_am_i)
{
	struct lis2dw12_emul_data *data = emul->data;

	data->who_am_i_reg = who_am_i;
}

uint32_t lis2dw12_emul_get_soft_reset_count(const struct emul *emul)
{
	struct lis2dw12_emul_data *data = emul->data;

	return data->soft_reset_count;
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
	case LIS2DW12_CTRL1_ADDR:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->ctrl1_reg;
		break;
	case LIS2DW12_CTRL2_ADDR:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->ctrl2_reg;
		break;
	case LIS2DW12_CTRL3_ADDR:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->ctrl3_reg;
		break;
	case LIS2DW12_CTRL6_ADDR:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->ctrl6_reg;
		break;
	case LIS2DW12_STATUS_REG:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->status_reg;
		break;
	case LIS2DW12_OUT_X_L_ADDR:
	case LIS2DW12_OUT_X_H_ADDR:
	case LIS2DW12_OUT_Y_L_ADDR:
	case LIS2DW12_OUT_Y_H_ADDR:
	case LIS2DW12_OUT_Z_L_ADDR:
	case LIS2DW12_OUT_Z_H_ADDR:
		/* Allow multi-byte reads within this range of registers.
		 * `bytes` is actually an offset past the starting register
		 * `reg`.
		 */

		__ASSERT_NO_MSG(LIS2DW12_OUT_X_L_ADDR + bytes <=
				LIS2DW12_OUT_Z_H_ADDR);

		/* 0 is OUT_X_L_ADDR .. 5 is OUT_Z_H_ADDR */
		int offset_into_odrs = reg - LIS2DW12_OUT_X_L_ADDR + bytes;

		/* Which of the 3 channels we're reading. 0 = X, 1 = Y, 2 = Z */
		int channel = offset_into_odrs / 2;

		if (offset_into_odrs % 2 == 0) {
			/* Get the LSB (L reg) */
			*val = data->accel_data[channel] & 0xFF;
		} else {
			/* Get the MSB (H reg) */
			*val = (data->accel_data[channel] >> 8) & 0xFF;
		}
		break;
	default:
		__ASSERT(false, "No read handler for register 0x%02x", reg);
		return -EINVAL;
	}
	return 0;
}

uint8_t lis2dw12_emul_peek_reg(struct i2c_emul *emul, int reg)
{
	__ASSERT(emul, "emul is NULL");

	uint8_t val;
	int rv;

	rv = lis2dw12_emul_read_byte(emul, reg, &val, 0);
	__ASSERT(rv == 0, "Read function returned non-zero: %d", rv);

	return val;
}

uint8_t lis2dw12_emul_peek_odr(struct i2c_emul *emul)
{
	__ASSERT(emul, "emul is NULL");

	uint8_t reg = lis2dw12_emul_peek_reg(emul, LIS2DW12_ACC_ODR_ADDR);

	return (reg & LIS2DW12_ACC_ODR_MASK) >>
	       __builtin_ctz(LIS2DW12_ACC_ODR_MASK);
}

uint8_t lis2dw12_emul_peek_mode(struct i2c_emul *emul)
{
	__ASSERT(emul, "emul is NULL");

	uint8_t reg = lis2dw12_emul_peek_reg(emul, LIS2DW12_ACC_MODE_ADDR);

	return (reg & LIS2DW12_ACC_MODE_MASK) >>
	       __builtin_ctz(LIS2DW12_ACC_MODE_MASK);
}

uint8_t lis2dw12_emul_peek_lpmode(struct i2c_emul *emul)
{
	__ASSERT(emul, "emul is NULL");

	uint8_t reg = lis2dw12_emul_peek_reg(emul, LIS2DW12_ACC_LPMODE_ADDR);

	return (reg & LIS2DW12_ACC_LPMODE_MASK);
}

static int lis2dw12_emul_write_byte(struct i2c_emul *emul, int reg, uint8_t val,
				    int bytes)
{
	struct lis2dw12_emul_data *data = LIS2DW12_DATA_FROM_I2C_EMUL(emul);

	switch (reg) {
	case LIS2DW12_WHO_AM_I_REG:
		LOG_ERR("Can't write to who-am-i register");
		return -EINVAL;
	case LIS2DW12_CTRL1_ADDR:
		data->ctrl1_reg = val;
		break;
	case LIS2DW12_CTRL2_ADDR:
		__ASSERT_NO_MSG(bytes == 1);
		if ((val & LIS2DW12_SOFT_RESET_MASK) != 0) {
			/* Soft reset */
			data->soft_reset_count++;
		}
		data->ctrl2_reg = val & ~LIS2DW12_SOFT_RESET_MASK;
		break;
	case LIS2DW12_CTRL3_ADDR:
		data->ctrl3_reg = val;
		break;
	case LIS2DW12_CTRL6_ADDR:
		data->ctrl6_reg = val;
		break;
	case LIS2DW12_STATUS_REG:
		__ASSERT(false,
			 "Attempt to write to read-only status register");
		return -EINVAL;
	case LIS2DW12_OUT_X_L_ADDR:
	case LIS2DW12_OUT_X_H_ADDR:
	case LIS2DW12_OUT_Y_L_ADDR:
	case LIS2DW12_OUT_Y_H_ADDR:
	case LIS2DW12_OUT_Z_L_ADDR:
	case LIS2DW12_OUT_Z_H_ADDR:
		__ASSERT(false,
			 "Attempt to write to data output register 0x%02x",
			 reg);
		return -EINVAL;
	default:
		__ASSERT(false, "No write handler for register 0x%02x", reg);
		return -EINVAL;
	}
	return 0;
}

static int emul_lis2dw12_init(const struct emul *emul,
			      const struct device *parent)
{
	const struct lis2dw12_emul_cfg *lis2dw12_cfg = emul->cfg;
	const struct i2c_common_emul_cfg *cfg = &(lis2dw12_cfg->common);
	struct lis2dw12_emul_data *data = emul->data;

	data->common.emul.api = &i2c_common_emul_api;
	data->common.emul.addr = cfg->addr;
	data->common.emul.parent = emul;
	data->common.i2c = parent;
	data->common.cfg = cfg;
	i2c_common_emul_init(&data->common);

	return i2c_emul_register(parent, emul->dev_label, &data->common.emul);
}

int lis2dw12_emul_set_accel_reading(const struct emul *emul, intv3_t reading)
{
	__ASSERT(emul, "emul is NULL");
	struct lis2dw12_emul_data *data = emul->data;

	for (int i = X; i <= Z; i++) {
		/* Ensure we fit in a 14-bit signed integer */
		if (reading[i] < LIS2DW12_SAMPLE_MIN ||
		    reading[i] > LIS2DW12_SAMPLE_MAX) {
			return -EINVAL;
		}
		/* Readings are left-aligned, so shift over by 2 */
		data->accel_data[i] = reading[i] * 4;
	}

	/* Set the DRDY (data ready) bit */
	data->status_reg |= LIS2DW12_STS_DRDY_UP;

	return 0;
}

void lis2dw12_emul_clear_accel_reading(const struct emul *emul)
{
	__ASSERT(emul, "emul is NULL");
	struct lis2dw12_emul_data *data = emul->data;

	/* Zero out the registers and reset DRDY bit */
	memset(data->accel_data, 0, sizeof(data->accel_data));
	data->status_reg &= ~LIS2DW12_STS_DRDY_UP;
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
