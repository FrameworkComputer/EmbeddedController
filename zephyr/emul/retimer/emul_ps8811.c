/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "driver/retimer/ps8811.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "emul/retimer/emul_ps8811.h"

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(ps8811_emul, CONFIG_RETIMER_EMUL_LOG_LEVEL);

#define DT_DRV_COMPAT cros_ps8811_emul

struct register_config {
	uint8_t reg;
	uint8_t def;
	uint8_t reserved;
};

static const struct register_config register_configs[] = {
	{
		.reg = PS8811_REG1_USB_AEQ_LEVEL,
		.def = PS8811_REG1_USB_AEQ_LEVEL_DEFAULT,
	},
	{
		.reg = PS8811_REG1_USB_ADE_CONFIG,
		.def = PS8811_REG1_USB_ADE_CONFIG_DEFAULT,
	},
	{
		.reg = PS8811_REG1_USB_BEQ_LEVEL,
		.def = PS8811_REG1_USB_BEQ_LEVEL_DEFAULT,
	},
	{
		.reg = PS8811_REG1_USB_BDE_CONFIG,
		.def = PS8811_REG1_USB_BDE_CONFIG_DEFAULT,
	},
	{
		.reg = PS8811_REG1_USB_CHAN_A_SWING,
		.def = PS8811_REG1_USB_CHAN_A_SWING_DEFAULT,
		.reserved = PS8811_REG1_USB_CHAN_A_SWING_RESERVED_MASK,
	},
	{
		.reg = PS8811_REG1_50OHM_ADJUST_CHAN_B,
		.def = PS8811_REG1_50OHM_ADJUST_CHAN_B_DEFAULT,
		.reserved = PS8811_REG1_50OHM_ADJUST_CHAN_B_RESERVED_MASK,
	},
	{
		.reg = PS8811_REG1_USB_CHAN_B_SWING,
		.def = PS8811_REG1_USB_CHAN_B_SWING_DEFAULT,
		.reserved = PS8811_REG1_USB_CHAN_B_SWING_RESERVED_MASK,
	},
	{
		.reg = PS8811_REG1_USB_CHAN_B_DE_PS_LSB,
		.def = PS8811_REG1_USB_CHAN_B_DE_PS_LSB_DEFAULT,
		.reserved = PS8811_REG1_USB_CHAN_B_DE_PS_LSB_RESERVED_MASK,
	},
	{
		.reg = PS8811_REG1_USB_CHAN_B_DE_PS_MSB,
		.def = PS8811_REG1_USB_CHAN_B_DE_PS_MSB_DEFAULT,
		.reserved = PS8811_REG1_USB_CHAN_B_DE_PS_MSB_RESERVED_MASK,
	},
};

static int ps8811_emul_p1_read_byte(const struct emul *emul, int reg,
				    uint8_t *val, int byte)
{
	/* register_configs are all one byte. */
	if (byte != 0)
		return -EIO;

	return (ps8811_emul_get_reg1(emul, reg, val) == 0) ? 0 : -EIO;
}

static int ps8811_emul_p1_write_byte(const struct emul *emul, int reg,
				     uint8_t val, int bytes)
{
	/* register_configs are all one byte. */
	if (bytes != 1)
		return -EIO;

	return (ps8811_emul_set_reg1(emul, reg, val) == 0) ? 0 : -EIO;
}

static int ps8811_emul_p0_read_byte(const struct emul *emul, int reg,
				    uint8_t *val, int byte)
{
	return (ps8811_emul_get_reg0(emul, reg, val) == 0) ? 0 : -EIO;
}

static int ps8811_emul_p0_write_byte(const struct emul *emul, int reg,
				     uint8_t val, int bytes)
{
	return (ps8811_emul_set_reg0(emul, reg, val) == 0) ? 0 : -EIO;
}

static int i2c_ps8811_emul_transfer(const struct emul *target,
				    struct i2c_msg *msgs, int num_msgs,
				    int addr)
{
	struct ps8811_emul_data *data = target->data;
	const struct ps8811_emul_cfg *cfg = target->cfg;

	if (addr == target->bus.i2c->addr) {
		return i2c_common_emul_transfer_workhorse(target, target->data,
							  target->cfg, msgs,
							  num_msgs, addr);
	} else if (addr == data->p1_data.emul.addr) {
		return i2c_common_emul_transfer_workhorse(target,
							  &data->p1_data,
							  &cfg->p1_cfg, msgs,
							  num_msgs, addr);
	}

	LOG_ERR("Cannot map address %02x", addr);
	return -EIO;
}

struct i2c_emul_api i2c_ps8811_emul_api = {
	.transfer = i2c_ps8811_emul_transfer,
};

static int ps8811_emul_init(const struct emul *emul,
			    const struct device *parent)
{
	struct ps8811_emul_data *data = emul->data;
	const struct ps8811_emul_cfg *cfg = emul->cfg;
	int rv = 0;

	ps8811_emul_reset(emul);

	i2c_common_emul_init(&data->p0_data);

	data->p1_data.emul.api = &i2c_ps8811_emul_api;
	data->p1_data.emul.addr = cfg->p1_cfg.addr;
	data->p1_data.emul.target = emul;
	data->p1_data.i2c = parent;
	data->p1_data.cfg = &cfg->p1_cfg;
	i2c_common_emul_init(&data->p1_data);

	rv = i2c_emul_register(parent, &data->p1_data.emul);
	if (rv) {
		LOG_ERR("Failed to register page 1 register emulator");
		return rv;
	}

	return 0;
}

/*
 * Page 0 contains hardware revision and similar info. No code currently
 * accesses these register_configs so just stub them out for future use.
 */
int ps8811_emul_get_reg0(const struct emul *emulator, int reg, uint8_t *val)
{
	return -EINVAL;
}

int ps8811_emul_set_reg0(const struct emul *emulator, int reg, uint8_t val)
{
	return -EINVAL;
}

static const struct register_config *get_reg1_config(int reg)
{
	for (size_t i = 0; i < ARRAY_SIZE(register_configs); i++) {
		if (register_configs[i].reg == reg)
			return &register_configs[i];
	}

	return NULL;
}

int ps8811_emul_get_reg1(const struct emul *emulator, int reg, uint8_t *val)
{
	struct ps8811_emul_data *ps8811 = emulator->data;

	if (reg >= ARRAY_SIZE(ps8811->p1_regs)) {
		LOG_DBG("Register %x is out of bounds");
		return -EINVAL;
	}

	if (!get_reg1_config(reg)) {
		LOG_DBG("Unknown register %x", reg);
		return -EINVAL;
	}

	*val = ps8811->p1_regs[reg];
	return 0;
}

int ps8811_emul_set_reg1(const struct emul *emulator, int reg, uint8_t val)
{
	struct ps8811_emul_data *ps8811 = emulator->data;
	const struct register_config *config = get_reg1_config(reg);

	if (reg >= ARRAY_SIZE(ps8811->p1_regs)) {
		LOG_DBG("Register %x is out of bounds");
		return -EINVAL;
	}

	if (!config) {
		LOG_DBG("Unknown register %x", reg);
		return -EINVAL;
	}

	/* Validate that we're not touching reserved bits. */
	if ((val & config->reserved) != (config->def & config->reserved)) {
		LOG_DBG("Reserved bits modified for reg %02x, val: %02x, \
			default: %02x, reserved: %02x",
			reg, val, config->def, config->reserved);
		return -EINVAL;
	}

	ps8811->p1_regs[reg] = val;
	return 0;
}

void ps8811_emul_reset(const struct emul *emul)
{
	/* Use the setter helps catch any default misconfigs. */
	for (size_t i = 0; i < ARRAY_SIZE(register_configs); i++)
		ps8811_emul_set_reg1(emul, register_configs[i].reg,
				     register_configs[i].def);
}

#define PS8811_EMUL_RESET_RULE_AFTER(n) \
	ps8811_emul_reset(EMUL_DT_GET(DT_DRV_INST(n)));

static void ps8811_emul_test_reset(const struct ztest_unit_test *test,
				   void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

	DT_INST_FOREACH_STATUS_OKAY(PS8811_EMUL_RESET_RULE_AFTER)
}

ZTEST_RULE(emul_ps8811_reset, NULL, ps8811_emul_test_reset);

#define PS8811_EMUL(n)                                                  \
	static struct ps8811_emul_data ps8811_emul_data_##n = { \
		.p0_data = { \
			.read_byte = ps8811_emul_p0_read_byte, \
			.write_byte = ps8811_emul_p0_write_byte, \
		}, \
		.p1_data = { \
			.read_byte = ps8811_emul_p1_read_byte, \
			.write_byte = ps8811_emul_p1_write_byte, \
		}, \
	};       \
	static const struct ps8811_emul_cfg ps8811_emul_cfg_##n = { \
		.p0_cfg = { \
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)), \
			.data = &ps8811_emul_data_##n.p0_data, \
			.addr = DT_INST_REG_ADDR(n), \
		}, \
		.p1_cfg = { \
			.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)), \
			.data = &ps8811_emul_data_##n.p1_data, \
			.addr = DT_INST_REG_ADDR(n) + 1, \
		}, \
	};   \
	EMUL_DT_INST_DEFINE(n, ps8811_emul_init, &ps8811_emul_data_##n, \
			    &ps8811_emul_cfg_##n, &i2c_ps8811_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(PS8811_EMUL);
DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE)
