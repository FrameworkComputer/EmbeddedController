/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ln9310_emul

#include <device.h>
#include <devicetree/gpio.h>
#include <drivers/gpio/gpio_emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>
#include <drivers/emul.h>
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

enum functional_mode {
	/* TODO shutdown_mode, */
	/* TODO bypass, */
	FUNCTIONAL_MODE_STANDBY = LN9310_SYS_STANDBY,
	FUNCTIONAL_MODE_SWITCHING_21 =
		LN9310_SYS_SWITCHING21_ACTIVE,
	FUNCTIONAL_MODE_SWITCHING_31 =
		LN9310_SYS_SWITCHING31_ACTIVE
};

struct ln9310_emul_data {
	/** Common I2C data */
	struct i2c_common_emul_data common;
	/** Emulated int_gpio port */
	const struct device *gpio_int_port;
	/** Emulated int_gpio pin */
	gpio_pin_t gpio_int_pin;
	/** The current emulated battery cell type */
	enum battery_cell_type battery_cell_type;
	/** Current Functional Mode */
	enum functional_mode current_mode;
	/** Emulated TEST MODE CTRL register */
	uint8_t test_mode_ctrl_reg;
	/** Emulated FORCE SC21 CTRL 1 register */
	uint8_t force_sc21_ctrl_1_reg;
	/** Emulated FORCE SC21 CTRL 2 register */
	uint8_t force_sc21_ctrl_2_reg;
	/** Emulated SYS STS register */
	uint8_t sys_sts_reg;
	/** Emulated INT1 MSK register */
	uint8_t int1_msk_reg;
	/** Emulated INT1 register */
	uint8_t int1_reg;
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
	/** Emulated mode change cfg register */
	uint8_t mode_change_cfg_reg;
	/** Emulated system control register */
	uint8_t sys_ctrl_reg;
};

static const struct emul *singleton;

struct i2c_emul *ln9310_emul_get_i2c_emul(const struct emul *emulator)
{
	struct ln9310_emul_data *data = emulator->data;

	return &(data->common.emul);
}

static void ln9310_emul_set_int_pin(struct ln9310_emul_data *data, bool val)
{
	int res = gpio_emul_input_set(data->gpio_int_port, data->gpio_int_pin,
				      val);
	__ASSERT_NO_MSG(res == 0);
}

static void ln9310_emul_assert_interrupt(struct ln9310_emul_data *data)
{
	data->int1_reg |= LN9310_INT1_MODE;
	ln9310_emul_set_int_pin(data, false);
}

static void ln9310_emul_deassert_interrupt(struct ln9310_emul_data *data)
{
	ln9310_emul_set_int_pin(data, true);
}

static void mode_change(struct ln9310_emul_data *data)
{
	bool new_mode_in_standby = data->startup_ctrl_reg &
				   LN9310_STARTUP_STANDBY_EN;
	bool new_mode_in_switching_21 =
		((data->power_ctrl_reg & LN9310_PWR_OP_MODE_MASK) ==
		 LN9310_PWR_OP_MODE_SWITCH21) &&
		!new_mode_in_standby;
	bool new_mode_in_switching_31 =
		((data->power_ctrl_reg & LN9310_PWR_OP_MODE_MASK) ==
		 LN9310_PWR_OP_MODE_SWITCH31) &&
		!new_mode_in_standby;

	__ASSERT_NO_MSG(
		!(new_mode_in_switching_21 && new_mode_in_switching_31));

	switch (data->current_mode) {
	case FUNCTIONAL_MODE_STANDBY:
		if (new_mode_in_switching_21) {
			data->current_mode = FUNCTIONAL_MODE_SWITCHING_21;
			data->sys_sts_reg = data->current_mode;
			ln9310_emul_assert_interrupt(data);
		} else if (new_mode_in_switching_31) {
			data->current_mode = FUNCTIONAL_MODE_SWITCHING_31;
			data->sys_sts_reg = data->current_mode;
			ln9310_emul_assert_interrupt(data);
		}
		break;
	case FUNCTIONAL_MODE_SWITCHING_21:
		if (new_mode_in_standby) {
			data->current_mode = FUNCTIONAL_MODE_STANDBY;
			data->sys_sts_reg = data->current_mode;
			ln9310_emul_assert_interrupt(data);
		} else if (new_mode_in_switching_31) {
			data->current_mode = FUNCTIONAL_MODE_SWITCHING_31;
			data->sys_sts_reg = data->current_mode;
			ln9310_emul_assert_interrupt(data);
		}
		break;
	case FUNCTIONAL_MODE_SWITCHING_31:
		if (new_mode_in_standby) {
			data->current_mode = FUNCTIONAL_MODE_STANDBY;
			data->sys_sts_reg = data->current_mode;
			ln9310_emul_assert_interrupt(data);
		} else if (new_mode_in_switching_21) {
			data->current_mode = FUNCTIONAL_MODE_SWITCHING_21;
			data->sys_sts_reg = data->current_mode;
			ln9310_emul_assert_interrupt(data);
		}
		break;
	default:
		__ASSERT(0, "Unrecognized mode");
	}
}

void ln9310_emul_set_context(const struct emul *emulator)
{
	singleton = emulator;
}

void ln9310_emul_reset(const struct emul *emulator)
{
	struct ln9310_emul_data *data = emulator->data;
	struct i2c_common_emul_data common = data->common;

	gpio_pin_t gpio_int_pin = data->gpio_int_pin;
	const struct device *gpio_int_port = data->gpio_int_port;

	/* Only Reset the LN9310 Register Data */
	memset(data, 0, sizeof(struct ln9310_emul_data));
	data->common = common;
	data->current_mode = LN9310_SYS_STANDBY;
	data->gpio_int_pin = gpio_int_pin;
	data->gpio_int_port = gpio_int_port;

	ln9310_emul_deassert_interrupt(data);
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
	struct ln9310_emul_data *data = emulator->data;

	bool interrupts_unmasked = (data->int1_msk_reg & LN9310_INT1_MODE) == 0;
	bool min_switch_freq_set =
		(data->spare_0_reg & LN9310_SPARE_0_LB_MIN_FREQ_SEL_ON) != 0;
	bool functional_mode_switching_21_enabled =
		(data->power_ctrl_reg & LN9310_PWR_OP_MODE_SWITCH21) != 0;
	bool functional_mode_switching_31_enabled =
		(data->power_ctrl_reg & LN9310_PWR_OP_MODE_SWITCH31) != 0;

	return interrupts_unmasked && min_switch_freq_set &&
	       (functional_mode_switching_21_enabled ||
		functional_mode_switching_31_enabled);
}

enum battery_cell_type board_get_battery_cell_type(void)
{
	struct ln9310_emul_data *data = singleton->data;

	return data->battery_cell_type;
}

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

	__ASSERT(bytes == 1, "bytes 0x%x != 0x1 on reg 0x%x", bytes, reg);

	switch (reg) {
	case LN9310_REG_INT1:
		data->int1_reg = val;
		break;
	case LN9310_REG_SYS_STS:
		data->sys_sts_reg = val;
		break;
	case LN9310_REG_INT1_MSK:
		data->int1_msk_reg = val;
		break;
	case LN9310_REG_STARTUP_CTRL:
		data->startup_ctrl_reg = val;
		break;
	case LN9310_REG_LION_CTRL:
		data->lion_ctrl_reg = val;
		break;
	case LN9310_REG_BC_STS_B:
		data->bc_sts_b_reg = val;
		break;
	case LN9310_REG_BC_STS_C:
		__ASSERT(false,
			 "Write to an unverified as safe "
			 "read-only register on 0x%x",
			 reg);
		break;
	case LN9310_REG_CFG_0:
		data->cfg_0_reg = val;
		break;
	case LN9310_REG_CFG_4:
		data->cfg_4_reg = val;
		break;
	case LN9310_REG_CFG_5:
		data->cfg_5_reg = val;
		break;
	case LN9310_REG_PWR_CTRL:
		data->power_ctrl_reg = val;
		break;
	case LN9310_REG_TIMER_CTRL:
		data->timer_ctrl_reg = val;
		break;
	case LN9310_REG_LB_CTRL:
		data->lower_bound_ctrl_reg = val;
		break;
	case LN9310_REG_SPARE_0:
		data->spare_0_reg = val;
		break;
	case LN9310_REG_SWAP_CTRL_0:
		data->swap_ctrl_0_reg = val;
		break;
	case LN9310_REG_SWAP_CTRL_1:
		data->swap_ctrl_1_reg = val;
		break;
	case LN9310_REG_SWAP_CTRL_2:
		data->swap_ctrl_2_reg = val;
		break;
	case LN9310_REG_SWAP_CTRL_3:
		data->swap_ctrl_3_reg = val;
		break;
	case LN9310_REG_TRACK_CTRL:
		data->track_ctrl_reg = val;
		break;
	case LN9310_REG_MODE_CHANGE_CFG:
		data->mode_change_cfg_reg = val;
		break;
	case LN9310_REG_SYS_CTRL:
		data->sys_ctrl_reg = val;
		break;
	case LN9310_REG_FORCE_SC21_CTRL_1:
		data->force_sc21_ctrl_1_reg = val;
		break;
	case LN9310_REG_FORCE_SC21_CTRL_2:
		data->force_sc21_ctrl_2_reg = val;
		break;
	case LN9310_REG_TEST_MODE_CTRL:
		data->test_mode_ctrl_reg = val;
		break;
	default:
		__ASSERT(false, "Unimplemented Register Access Error on 0x%x",
			 reg);
	}
	mode_change(data);
	return 0;
}

static int ln9310_emul_start_read(struct i2c_emul *emul, int reg)
{
	return 0;
}

static int ln9310_emul_finish_read(struct i2c_emul *emul, int reg, int bytes)
{
	struct ln9310_emul_data *data = LN9310_DATA_FROM_I2C_EMUL(emul);

	switch (reg) {
	case LN9310_REG_INT1:
		/* Reading the interrupt clears it. */
		data->int1_reg = 0;
		break;
	}
	return 0;
}

static int ln9310_emul_read_byte(struct i2c_emul *emul, int reg, uint8_t *val,
				 int bytes)
{
	struct ln9310_emul_data *data = LN9310_DATA_FROM_I2C_EMUL(emul);

	__ASSERT(bytes == 0, "bytes 0x%x != 0x0 on reg 0x%x", bytes, reg);

	switch (reg) {
	case LN9310_REG_INT1:
		*val = data->int1_reg;
		/* Reading clears interrupts */
		data->int1_reg = 0;
		ln9310_emul_deassert_interrupt(data);
		break;
	case LN9310_REG_SYS_STS:
		*val = data->sys_sts_reg;
		break;
	case LN9310_REG_INT1_MSK:
		*val = data->int1_msk_reg;
		break;
	case LN9310_REG_STARTUP_CTRL:
		*val = data->startup_ctrl_reg;
		break;
	case LN9310_REG_LION_CTRL:
		*val = data->lion_ctrl_reg;
		break;
	case LN9310_REG_BC_STS_B:
		*val = data->bc_sts_b_reg;
		break;
	case LN9310_REG_BC_STS_C:
		*val = data->bc_sts_c_reg;
		break;
	case LN9310_REG_CFG_0:
		*val = data->cfg_0_reg;
		break;
	case LN9310_REG_CFG_4:
		*val = data->cfg_4_reg;
		break;
	case LN9310_REG_CFG_5:
		*val = data->cfg_5_reg;
		break;
	case LN9310_REG_PWR_CTRL:
		*val = data->power_ctrl_reg;
		break;
	case LN9310_REG_TIMER_CTRL:
		*val = data->timer_ctrl_reg;
		break;
	case LN9310_REG_LB_CTRL:
		*val = data->lower_bound_ctrl_reg;
		break;
	case LN9310_REG_SPARE_0:
		*val = data->spare_0_reg;
		break;
	case LN9310_REG_SWAP_CTRL_0:
		*val = data->swap_ctrl_0_reg;
		break;
	case LN9310_REG_SWAP_CTRL_1:
		*val = data->swap_ctrl_1_reg;
		break;
	case LN9310_REG_SWAP_CTRL_2:
		*val = data->swap_ctrl_2_reg;
		break;
	case LN9310_REG_SWAP_CTRL_3:
		*val = data->swap_ctrl_3_reg;
		break;
	case LN9310_REG_TRACK_CTRL:
		*val = data->track_ctrl_reg;
		break;
	case LN9310_REG_MODE_CHANGE_CFG:
		*val = data->mode_change_cfg_reg;
		break;
	case LN9310_REG_SYS_CTRL:
		*val = data->sys_ctrl_reg;
		break;
	case LN9310_REG_FORCE_SC21_CTRL_1:
		*val = data->force_sc21_ctrl_1_reg;
		break;
	case LN9310_REG_FORCE_SC21_CTRL_2:
		*val = data->force_sc21_ctrl_2_reg;
		break;
	case LN9310_REG_TEST_MODE_CTRL:
		*val = data->test_mode_ctrl_reg;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ln9310_emul_access_reg(struct i2c_emul *emul, int reg, int bytes,
				  bool read)
{
	return reg;
}

static int emul_ln9310_init(const struct emul *emul,
			    const struct device *parent)
{
	const struct i2c_common_emul_cfg *cfg = emul->cfg;
	struct ln9310_emul_data *data = emul->data;

	data->common.emul.api = &i2c_common_emul_api;
	data->common.emul.addr = cfg->addr;
	data->common.emul.parent = emul;
	data->common.i2c = parent;
	data->common.cfg = cfg;
	i2c_common_emul_init(&data->common);

	singleton = emul;

	return i2c_emul_register(parent, emul->dev_label, &data->common.emul);
}

#define LN9310_GET_GPIO_INT_PORT(n) \
	DEVICE_DT_GET(DT_GPIO_CTLR(DT_INST_PROP(n, pg_int_gpio), gpios))

#define LN9310_GET_GPIO_INT_PIN(n) \
	DT_GPIO_PIN(DT_INST_PROP(n, pg_int_gpio), gpios)

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
		.gpio_int_port = LN9310_GET_GPIO_INT_PORT(n),		       \
		.gpio_int_pin = LN9310_GET_GPIO_INT_PIN(n),		       \
	};                                                                     \
	static const struct i2c_common_emul_cfg ln9310_emul_cfg_##n = {        \
		.i2c_label = DT_INST_BUS_LABEL(n),                             \
		.dev_label = DT_INST_LABEL(n),                                 \
		.addr = DT_INST_REG_ADDR(n),                                   \
	};                                                                     \
	EMUL_DEFINE(emul_ln9310_init, DT_DRV_INST(n), &ln9310_emul_cfg_##n,    \
		    &ln9310_emul_data_##n)

DT_INST_FOREACH_STATUS_OKAY(INIT_LN9310)
