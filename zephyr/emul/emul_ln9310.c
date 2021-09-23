/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ln9310_emul

#include <device.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>
#include <emul.h>
#include <errno.h>
#include <sys/__assert.h>

#include "driver/ln9310.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_ln9310.h"
#include "i2c.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(ln9310_emul, CONFIG_LN9310_EMUL_LOG_LEVEL);

#define LN9310_DATA_FROM_I2C_EMUL(_emul)                                     \
	CONTAINER_OF(CONTAINER_OF(_emul, struct i2c_common_emul_data, emul), \
		     struct ln9310_emul_data, common)

struct ln9310_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;
	/** The current emulated battery cell type */
	enum battery_cell_type battery_cell_type;
	/** Emulated INT1 MSK register */
	uint8_t int1_msk_reg;
	/** Emulated Lion control register */
	uint8_t lion_ctrl_reg;
	/** Emulated startup control register */
	uint8_t startup_ctrl_reg;
	/** Emulated BC STST B register */
	uint8_t bc_sts_b_reg;
	/** Emulated BC STST C register */
	uint8_t bc_sts_c_reg;
	/** Emulated cfg 0 register */
	uint8_t cfg_0_reg;
	/** Emulated cfg 4 register */
	uint8_t cfg_4_reg;
	/** Emulated cfg 5 register */
	uint8_t cfg_5_reg;
	/** Emulated power control register */
	uint8_t power_ctrl_reg;
	/** Emulated timer control register */
	uint8_t timer_ctrl_reg;
	/** Emulated lower bound (LB) control register */
	uint8_t lower_bound_ctrl_reg;
	/** Emulated spare 0 register */
	uint8_t spare_0_reg;
	/** Emulated swap control 0 register */
	uint8_t swap_ctrl_0_reg;
	/** Emulated swap control 1 register */
	uint8_t swap_ctrl_1_reg;
	/** Emulated swap control 2 register */
	uint8_t swap_ctrl_2_reg;
	/** Emulated swap control 3 register */
	uint8_t swap_ctrl_3_reg;
	/** Emulated track control register */
	uint8_t track_ctrl_reg;
	/** Emulated mode change register */
	uint8_t mode_change_reg;
	/** Emulated system control register */
	uint8_t sys_ctrl_reg;
};

static const struct emul *singleton;

void ln9310_emul_set_context(const struct emul *emulator)
{
	singleton = emulator;
}

void ln9310_emul_reset(const struct emul *emulator)
{
	struct ln9310_emul_data *data = emulator->data;

	data->lion_ctrl_reg = 0;
	data->startup_ctrl_reg = 0;
	data->bc_sts_b_reg = 0;
	data->cfg_0_reg = 0;
	data->cfg_4_reg = 0;
	data->cfg_5_reg = 0;
	data->power_ctrl_reg = 0;
	data->timer_ctrl_reg = 0;
	data->lower_bound_ctrl_reg = 0;
	data->spare_0_reg = 0;
	data->swap_ctrl_0_reg = 0;
	data->swap_ctrl_1_reg = 0;
	data->swap_ctrl_2_reg = 0;
	data->swap_ctrl_3_reg = 0;
	data->track_ctrl_reg = 0;
	data->mode_change_reg = 0;
	data->sys_ctrl_reg = 0;
}

void ln9310_emul_set_battery_cell_type(const struct emul *emulator,
				       enum battery_cell_type type)
{
	struct ln9310_emul_data *data = emulator->data;

	data->battery_cell_type = type;
}

void ln9310_emul_set_version(const struct emul *emulator, int version)
{
	struct ln9310_emul_data *data = emulator->data;

	data->bc_sts_c_reg |= version & LN9310_BC_STS_C_CHIP_REV_MASK;
}

void ln9310_emul_set_vin_gt_10v(const struct emul *emulator, bool is_gt_10v)
{
	struct ln9310_emul_data *data = emulator->data;

	if (is_gt_10v)
		data->bc_sts_b_reg |= LN9310_BC_STS_B_INFET_OUT_SWITCH_OK;
	else
		data->bc_sts_b_reg &= ~LN9310_BC_STS_B_INFET_OUT_SWITCH_OK;
}

bool ln9310_emul_is_init(const struct emul *emulator)
{
	struct ln9310_emul_data *data =  emulator->data;

	return (data->int1_msk_reg & LN9310_INT1_MODE) == 0;
}

enum battery_cell_type board_get_battery_cell_type(void)
{
	struct ln9310_emul_data *data = singleton->data;

	return data->battery_cell_type;
}

static struct i2c_emul_api ln9310_emul_api_i2c = {
	.transfer = i2c_common_emul_transfer,
};

static int ln9310_emul_start_write(struct i2c_emul *emul, int reg)
{
	return 0;
}

static int ln9310_emul_finish_write(struct i2c_emul *emul, int reg, int bytes)
{
	return 0;
}

static int ln9310_emul_write_byte(struct i2c_emul *emul, int reg, uint8_t val,
				  int bytes)
{
	struct ln9310_emul_data *data = LN9310_DATA_FROM_I2C_EMUL(emul);

	switch (reg) {
	case LN9310_REG_INT1_MSK:
		__ASSERT_NO_MSG(bytes == 1);
		data->int1_msk_reg = val;
		break;
	case LN9310_REG_STARTUP_CTRL:
		__ASSERT_NO_MSG(bytes == 1);
		data->startup_ctrl_reg = val;
		break;
	case LN9310_REG_LION_CTRL:
		__ASSERT_NO_MSG(bytes == 1);
		data->lion_ctrl_reg = val;
		break;
	case LN9310_REG_BC_STS_B:
		__ASSERT_NO_MSG(bytes == 1);
		data->bc_sts_b_reg = val;
		break;
	case LN9310_REG_BC_STS_C:
		LOG_ERR("Can't write to BC STS C register");
		return -EINVAL;
	case LN9310_REG_CFG_0:
		__ASSERT_NO_MSG(bytes == 1);
		data->cfg_0_reg = val;
		break;
	case LN9310_REG_CFG_4:
		__ASSERT_NO_MSG(bytes == 1);
		data->cfg_4_reg = val;
		break;
	case LN9310_REG_CFG_5:
		__ASSERT_NO_MSG(bytes == 1);
		data->cfg_5_reg = val;
		break;
	case LN9310_REG_PWR_CTRL:
		__ASSERT_NO_MSG(bytes == 1);
		data->power_ctrl_reg = val;
		break;
	case LN9310_REG_TIMER_CTRL:
		__ASSERT_NO_MSG(bytes == 1);
		data->timer_ctrl_reg = val;
		break;
	case LN9310_REG_LB_CTRL:
		__ASSERT_NO_MSG(bytes = 1);
		data->lower_bound_ctrl_reg = val;
		break;
	case LN9310_REG_SPARE_0:
		__ASSERT_NO_MSG(bytes == 1);
		data->spare_0_reg = val;
		break;
	case LN9310_REG_SWAP_CTRL_0:
		__ASSERT_NO_MSG(bytes == 1);
		data->swap_ctrl_0_reg = val;
		break;
	case LN9310_REG_SWAP_CTRL_1:
		__ASSERT_NO_MSG(bytes == 1);
		data->swap_ctrl_1_reg = val;
		break;
	case LN9310_REG_SWAP_CTRL_2:
		__ASSERT_NO_MSG(bytes == 1);
		data->swap_ctrl_2_reg = val;
		break;
	case LN9310_REG_SWAP_CTRL_3:
		__ASSERT_NO_MSG(bytes == 1);
		data->swap_ctrl_3_reg = val;
		break;
	case LN9310_REG_TRACK_CTRL:
		__ASSERT_NO_MSG(bytes == 1);
		data->track_ctrl_reg = val;
		break;
	case LN9310_REG_MODE_CHANGE_CFG:
		__ASSERT_NO_MSG(bytes == 1);
		data->mode_change_reg = val;
		break;
	case LN9310_REG_SYS_CTRL:
		__ASSERT_NO_MSG(bytes == 1);
		data->sys_ctrl_reg = val;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ln9310_emul_start_read(struct i2c_emul *emul, int reg)
{
	return 0;
}

static int ln9310_emul_finish_read(struct i2c_emul *emul, int reg, int bytes)
{
	return 0;
}

static int ln9310_emul_read_byte(struct i2c_emul *emul, int reg, uint8_t *val,
				 int bytes)
{
	struct ln9310_emul_data *data = LN9310_DATA_FROM_I2C_EMUL(emul);

	switch (reg) {
	case LN9310_REG_INT1_MSK:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->int1_msk_reg;
		break;
	case LN9310_REG_STARTUP_CTRL:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->startup_ctrl_reg;
		break;
	case LN9310_REG_LION_CTRL:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->lion_ctrl_reg;
		break;
	case LN9310_REG_BC_STS_B:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->bc_sts_b_reg;
		break;
	case LN9310_REG_BC_STS_C:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->bc_sts_c_reg;
		break;
	case LN9310_REG_CFG_0:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->cfg_0_reg;
		break;
	case LN9310_REG_CFG_4:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->cfg_4_reg;
		break;
	case LN9310_REG_CFG_5:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->cfg_5_reg;
		break;
	case LN9310_REG_PWR_CTRL:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->power_ctrl_reg;
		break;
	case LN9310_REG_TIMER_CTRL:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->timer_ctrl_reg;
		break;
	case LN9310_REG_LB_CTRL:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->lower_bound_ctrl_reg;
		break;
	case LN9310_REG_SPARE_0:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->spare_0_reg;
		break;
	case LN9310_REG_SWAP_CTRL_0:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->swap_ctrl_0_reg;
		break;
	case LN9310_REG_SWAP_CTRL_1:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->swap_ctrl_1_reg;
		break;
	case LN9310_REG_SWAP_CTRL_2:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->swap_ctrl_2_reg;
		break;
	case LN9310_REG_SWAP_CTRL_3:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->swap_ctrl_3_reg;
		break;
	case LN9310_REG_TRACK_CTRL:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->track_ctrl_reg;
		break;
	case LN9310_REG_MODE_CHANGE_CFG:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->mode_change_reg;
		break;
	case LN9310_REG_SYS_CTRL:
		__ASSERT_NO_MSG(bytes == 0);
		*val = data->sys_ctrl_reg;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ln9310_emul_access_reg(struct i2c_emul *emul, int reg, int bytes,
				  bool read)
{
	return 0;
}

static int emul_ln9310_init(const struct emul *emul,
			    const struct device *parent)
{
	const struct i2c_common_emul_cfg *cfg = emul->cfg;
	struct ln9310_emul_data *data = emul->data;

	data->common.emul.api = &ln9310_emul_api_i2c;
	data->common.emul.addr = cfg->addr;
	data->common.emul.parent = emul;
	data->common.i2c = parent;
	data->common.cfg = cfg;
	i2c_common_emul_init(&data->common);

	singleton = emul;

	return i2c_emul_register(parent, emul->dev_label, &data->common.emul);
}

#define INIT_LN9310(n)                                                         \
	const struct ln9310_config_t ln9310_config = {                         \
		.i2c_port = NAMED_I2C(power),                                  \
		.i2c_addr_flags = DT_INST_REG_ADDR(n),                         \
	};                                                                     \
	static struct ln9310_emul_data ln9310_emul_data_##n = {                \
		.common = {                                                    \
			.start_write = ln9310_emul_start_write,                \
			.write_byte = ln9310_emul_write_byte,                  \
			.finish_write = ln9310_emul_finish_write,              \
			.start_read = ln9310_emul_start_read,                  \
			.read_byte = ln9310_emul_read_byte,                    \
			.finish_read = ln9310_emul_finish_read,                \
			.access_reg = ln9310_emul_access_reg,                  \
		},                                                             \
	};                                                                     \
	static const struct i2c_common_emul_cfg ln9310_emul_cfg_##n = {        \
		.i2c_label = DT_INST_BUS_LABEL(n),                             \
		.dev_label = DT_INST_LABEL(n),                                 \
		.addr = DT_INST_REG_ADDR(n),                                   \
	};                                                                     \
	EMUL_DEFINE(emul_ln9310_init, DT_DRV_INST(n), &ln9310_emul_cfg_##n,    \
		    &ln9310_emul_data_##n)

DT_INST_FOREACH_STATUS_OKAY(INIT_LN9310)
