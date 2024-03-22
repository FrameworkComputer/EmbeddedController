/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "driver/retimer/anx7483.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "emul/retimer/emul_anx7483.h"

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(anx7483_emul, CONFIG_RETIMER_EMUL_LOG_LEVEL);

#define DT_DRV_COMPAT cros_anx7483_emul

static const struct anx7483_register default_reg_configs[ANX7483_REG_MAX] = {
	{
		.reg = ANX7483_LFPS_TIMER_REG,
		.def = ANX7483_LFPS_TIMER_REG_DEFAULT,
		.reserved = ANX7483_LFPS_TIMER_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_ANALOG_STATUS_CTRL_REG,
		.def = ANX7483_ANALOG_STATUS_CTRL_REG_DEFAULT,
		.reserved = ANX7483_ANALOG_STATUS_CTRL_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_ENABLE_EQ_FLAT_SWING_REG,
		.def = ANX7483_ENABLE_EQ_FLAT_SWING_REG_DEFAULT,
		.reserved = ANX7483_ENABLE_EQ_FLAT_SWING_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_AUX_SNOOPING_CTRL_REG,
		.def = ANX7483_AUX_SNOOPING_CTRL_REG_DEFAULT,
		.reserved = ANX7483_AUX_SNOOPING_CTRL_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_CHIP_ID,
		.def = ANX7483_CHIP_ID_DEFAULT,
	},
	/* CFG0 */
	{
		.reg = ANX7483_UTX1_PORT_CFG0_REG,
		.def = ANX7483_UTX1_PORT_CFG0_REG_DEFAULT,
		.reserved = ANX7483_UTX1_PORT_CFG0_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_UTX2_PORT_CFG0_REG,
		.def = ANX7483_UTX2_PORT_CFG0_REG_DEFAULT,
		.reserved = ANX7483_UTX2_PORT_CFG0_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_URX1_PORT_CFG0_REG,
		.def = ANX7483_URX1_PORT_CFG0_REG_DEFAULT,
		.reserved = ANX7483_URX1_PORT_CFG0_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_URX2_PORT_CFG0_REG,
		.def = ANX7483_URX2_PORT_CFG0_REG_DEFAULT,
		.reserved = ANX7483_URX2_PORT_CFG0_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_DRX1_PORT_CFG0_REG,
		.def = ANX7483_DRX1_PORT_CFG0_REG_DEFAULT,
		.reserved = ANX7483_DRX1_PORT_CFG0_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_DRX2_PORT_CFG0_REG,
		.def = ANX7483_DRX2_PORT_CFG0_REG_DEFAULT,
		.reserved = ANX7483_DRX2_PORT_CFG0_REG_RESERVED_MASK,
	},

	/* CFG1 */
	{
		.reg = ANX7483_UTX1_PORT_CFG1_REG,
		.def = ANX7483_UTX1_PORT_CFG1_REG_DEFAULT,
	},
	{
		.reg = ANX7483_UTX2_PORT_CFG1_REG,
		.def = ANX7483_UTX2_PORT_CFG1_REG_DEFAULT,
	},
	{
		.reg = ANX7483_URX1_PORT_CFG1_REG,
		.def = ANX7483_URX1_PORT_CFG1_REG_DEFAULT,
	},
	{
		.reg = ANX7483_URX2_PORT_CFG1_REG,
		.def = ANX7483_URX2_PORT_CFG1_REG_DEFAULT,
	},
	{
		.reg = ANX7483_DRX1_PORT_CFG1_REG,
		.def = ANX7483_DRX1_PORT_CFG1_REG_DEFAULT,
	},
	{
		.reg = ANX7483_DRX2_PORT_CFG1_REG,
		.def = ANX7483_DRX2_PORT_CFG1_REG_DEFAULT,
	},
	{
		.reg = ANX7483_AUX_CFG_0,
		.def = ANX7483_AUX_CFG_0_DEFAULT,
	},
	{
		.reg = ANX7483_AUX_CFG_1,
		.def = ANX7483_AUX_CFG_1_DEFAULT,
	},

	/* CFG2 */
	{
		.reg = ANX7483_UTX1_PORT_CFG2_REG,
		.def = ANX7483_UTX1_PORT_CFG2_REG_DEFAULT,
		.reserved = ANX7483_UTX1_PORT_CFG2_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_UTX2_PORT_CFG2_REG,
		.def = ANX7483_UTX2_PORT_CFG2_REG_DEFAULT,
		.reserved = ANX7483_UTX2_PORT_CFG2_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_URX1_PORT_CFG2_REG,
		.def = ANX7483_URX1_PORT_CFG2_REG_DEFAULT,
		.reserved = ANX7483_URX1_PORT_CFG2_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_URX2_PORT_CFG2_REG,
		.def = ANX7483_URX2_PORT_CFG2_REG_DEFAULT,
		.reserved = ANX7483_URX2_PORT_CFG2_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_DRX1_PORT_CFG2_REG,
		.def = ANX7483_DRX1_PORT_CFG2_REG_DEFAULT,
		.reserved = ANX7483_DRX1_PORT_CFG2_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_DRX2_PORT_CFG2_REG,
		.def = ANX7483_DRX2_PORT_CFG2_REG_DEFAULT,
		.reserved = ANX7483_DRX2_PORT_CFG2_REG_RESERVED_MASK,
	},

	/* CFG3 */
	{
		.reg = ANX7483_UTX1_PORT_CFG3_REG,
		.def = ANX7483_UTX1_PORT_CFG3_REG_DEFAULT,
	},
	{
		.reg = ANX7483_UTX2_PORT_CFG3_REG,
		.def = ANX7483_UTX2_PORT_CFG3_REG_DEFAULT,
	},
	{
		.reg = ANX7483_URX1_PORT_CFG3_REG,
		.def = ANX7483_URX1_PORT_CFG3_REG_DEFAULT,
	},
	{
		.reg = ANX7483_URX2_PORT_CFG3_REG,
		.def = ANX7483_URX2_PORT_CFG3_REG_DEFAULT,
	},
	{
		.reg = ANX7483_DRX1_PORT_CFG3_REG,
		.def = ANX7483_DRX1_PORT_CFG3_REG_DEFAULT,
	},
	{
		.reg = ANX7483_DRX2_PORT_CFG3_REG,
		.def = ANX7483_DRX2_PORT_CFG3_REG_DEFAULT,
	},
	{
		.reg = ANX7483_DTX1_PORT_CFG3_REG,
		.def = ANX7483_DTX1_PORT_CFG3_REG_DEFAULT,
	},
	{
		.reg = ANX7483_DTX2_PORT_CFG3_REG,
		.def = ANX7483_DTX2_PORT_CFG3_REG_DEFAULT,
	},

	/* CFG4 */
	{
		.reg = ANX7483_UTX1_PORT_CFG4_REG,
		.def = ANX7483_UTX1_PORT_CFG4_REG_DEFAULT,
		.reserved = ANX7483_UTX1_PORT_CFG4_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_UTX2_PORT_CFG4_REG,
		.def = ANX7483_UTX2_PORT_CFG4_REG_DEFAULT,
		.reserved = ANX7483_UTX2_PORT_CFG4_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_URX1_PORT_CFG4_REG,
		.def = ANX7483_URX1_PORT_CFG4_REG_DEFAULT,
		.reserved = ANX7483_URX1_PORT_CFG4_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_URX2_PORT_CFG4_REG,
		.def = ANX7483_URX2_PORT_CFG4_REG_DEFAULT,
		.reserved = ANX7483_URX2_PORT_CFG4_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_DRX1_PORT_CFG4_REG,
		.def = ANX7483_DRX1_PORT_CFG4_REG_DEFAULT,
		.reserved = ANX7483_DRX1_PORT_CFG4_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_DRX2_PORT_CFG4_REG,
		.def = ANX7483_DRX2_PORT_CFG4_REG_DEFAULT,
		.reserved = ANX7483_DRX2_PORT_CFG4_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_DTX1_PORT_CFG4_REG,
		.def = ANX7483_DTX1_PORT_CFG4_REG_DEFAULT,
		.reserved = ANX7483_DTX1_PORT_CFG4_REG_RESERVED_MASK,
	},
	{
		.reg = ANX7483_DTX2_PORT_CFG4_REG,
		.def = ANX7483_DTX2_PORT_CFG4_REG_DEFAULT,
		.reserved = ANX7483_DTX2_PORT_CFG4_REG_RESERVED_MASK,
	},

};

static int anx7483_emul_read_byte(const struct emul *emul, int reg,
				  uint8_t *val, int byte)
{
	/* Registers are only one byte. */
	if (byte != 0)
		return -EIO;

	return (anx7483_emul_get_reg(emul, reg, val) == 0) ? 0 : -EIO;
}

static int anx7483_emul_write_byte(const struct emul *emul, int reg,
				   uint8_t val, int bytes)
{
	/* Registers are only one byte. */
	if (bytes != 1)
		return -EIO;
	return (anx7483_emul_set_reg(emul, reg, val) == 0) ? 0 : -EIO;
}

static struct anx7483_register *get_register_mut(const struct emul *emul,
						 int reg)
{
	struct anx7483_emul_data *anx7483 = emul->data;

	for (size_t i = 0; i < ANX7483_REG_MAX; i++) {
		if (anx7483->regs[i].reg == reg)
			return &anx7483->regs[i];
	}

	return NULL;
}

static const struct anx7483_register *
get_register_const(const struct emul *emul, int reg)
{
	return get_register_mut(emul, reg);
}

int anx7483_emul_get_reg(const struct emul *emul, int r, uint8_t *val)
{
	const struct anx7483_register *reg = get_register_const(emul, r);

	if (!reg) {
		LOG_DBG("Unknown register %x", r);
		return -EINVAL;
	}

	*val = reg->value;
	return 0;
}

int anx7483_emul_set_reg(const struct emul *emul, int r, uint8_t val)
{
	struct anx7483_register *reg = get_register_mut(emul, r);

	if (!reg) {
		LOG_DBG("Unknown register %x", r);
		return -EINVAL;
	}

	if ((val & reg->reserved) != (reg->def & reg->reserved)) {
		LOG_DBG("Reserved bits modified for reg %02x, val: %02x, \
			default: %02x, reserved: %02x",
			r, val, reg->def, reg->reserved);
		return -EINVAL;
	}

	reg->value = val;
	return 0;
}

int anx7483_emul_set_reg_reserved_mask(const struct emul *emul, int r,
				       uint8_t mask, uint8_t def)
{
	struct anx7483_register *reg = get_register_mut(emul, r);

	if (!reg) {
		LOG_DBG("Unknown register %x", r);
		return -EINVAL;
	}

	LOG_DBG("Overwriting reserved mask value for reg: %02x from %x to %x",
		r, reg->reserved, mask);
	reg->reserved = mask;
	reg->def = def;
	return 0;
}

int anx7483_emul_get_eq(const struct emul *emul, enum anx7483_tune_pin pin,
			enum anx7483_eq_setting *eq)
{
	int reg;
	int rv;

	if (pin == ANX7483_PIN_UTX1)
		reg = ANX7483_UTX1_PORT_CFG0_REG;
	else if (pin == ANX7483_PIN_UTX2)
		reg = ANX7483_UTX2_PORT_CFG0_REG;
	else if (pin == ANX7483_PIN_URX1)
		reg = ANX7483_URX1_PORT_CFG0_REG;
	else if (pin == ANX7483_PIN_URX2)
		reg = ANX7483_URX2_PORT_CFG0_REG;
	else if (pin == ANX7483_PIN_DRX1)
		reg = ANX7483_DRX1_PORT_CFG0_REG;
	else if (pin == ANX7483_PIN_DRX2)
		reg = ANX7483_DRX2_PORT_CFG0_REG;
	else
		return EC_ERROR_INVAL;

	rv = anx7483_emul_get_reg(emul, reg, (uint8_t *)eq);
	if (rv)
		return rv;

	*eq &= ANX7483_CFG0_EQ_MASK;
	*eq >>= ANX7483_CFG0_EQ_SHIFT;

	return EC_SUCCESS;
}

int anx7483_emul_get_fg(const struct emul *emul, enum anx7483_tune_pin pin,
			enum anx7483_fg_setting *fg)
{
	int reg;
	int rv;

	if (pin == ANX7483_PIN_UTX1)
		reg = ANX7483_UTX1_PORT_CFG2_REG;
	else if (pin == ANX7483_PIN_UTX2)
		reg = ANX7483_UTX2_PORT_CFG2_REG;
	else if (pin == ANX7483_PIN_URX1)
		reg = ANX7483_URX1_PORT_CFG2_REG;
	else if (pin == ANX7483_PIN_URX2)
		reg = ANX7483_URX2_PORT_CFG2_REG;
	else if (pin == ANX7483_PIN_DRX1)
		reg = ANX7483_DRX1_PORT_CFG2_REG;
	else if (pin == ANX7483_PIN_DRX2)
		reg = ANX7483_DRX2_PORT_CFG2_REG;
	else
		return EC_ERROR_INVAL;

	rv = anx7483_emul_get_reg(emul, reg, (uint8_t *)fg);
	if (rv)
		return rv;

	*fg &= ANX7483_CFG2_FG_MASK;
	*fg >>= ANX7483_CFG2_FG_SHIFT;

	return EC_SUCCESS;
}

void anx7483_emul_reset(const struct emul *emul)
{
	struct anx7483_emul_data *anx7483 = emul->data;

	/* Initialize our default register config. */
	memcpy(&anx7483->regs, default_reg_configs,
	       ANX7483_REG_MAX * sizeof(struct anx7483_register));

	/* Using the setter helps catch any default misconfigs. */
	for (size_t i = 0; i < ANX7483_REG_MAX; i++)
		anx7483_emul_set_reg(emul, anx7483->regs[i].reg,
				     anx7483->regs[i].def);
}

int anx7483_emul_validate_tuning(const struct emul *emul,
				 const struct anx7483_tuning_set *tuning,
				 size_t tuning_count)
{
	uint8_t val;
	int rv;

	for (size_t i = 0; i < tuning_count; i++) {
		rv = anx7483_emul_get_reg(emul, tuning[i].addr, &val);
		if (rv)
			return rv;

		if (val != tuning[i].value)
			return 1;
	}

	return 0;
}

static int anx7483_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	struct anx7483_emul_data *anx7483 = emul->data;

	anx7483_emul_reset(emul);
	i2c_common_emul_init(&anx7483->common);

	return 0;
}

#define ANX7483_EMUL_RESET_RULE_AFTER(n) \
	anx7483_emul_reset(EMUL_DT_GET(DT_DRV_INST(n)));

static void anx7483_emul_test_reset(const struct ztest_unit_test *test,
				    void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	DT_INST_FOREACH_STATUS_OKAY(ANX7483_EMUL_RESET_RULE_AFTER)
}

ZTEST_RULE(emul_anx7483_reset, NULL, anx7483_emul_test_reset);

#define ANX7483_EMUL(n)                                                   \
	static struct anx7483_emul_data anx7483_emul_data_##n = { \
		.common = { \
			.read_byte = anx7483_emul_read_byte, \
			.write_byte = anx7483_emul_write_byte, \
		}, \
	};       \
	static const struct anx7483_emul_cfg anx7483_emul_cfg_##n = { \
		.common = { \
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)), \
			.data = &anx7483_emul_data_##n.common, \
			.addr = DT_INST_REG_ADDR(n), \
		}, \
	};   \
	EMUL_DT_INST_DEFINE(n, anx7483_emul_init, &anx7483_emul_data_##n, \
			    &anx7483_emul_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(ANX7483_EMUL)
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE)
