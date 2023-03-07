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

struct register_config {
	uint8_t reg;
	uint8_t def;
	uint8_t reserved;
};

static const struct register_config register_configs[] = {
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

static const struct register_config *get_reg_config(int reg)
{
	for (size_t i = 0; i < ARRAY_SIZE(register_configs); i++) {
		if (register_configs[i].reg == reg)
			return &register_configs[i];
	}

	return NULL;
}

int anx7483_emul_get_reg(const struct emul *emulator, int reg, uint8_t *val)
{
	struct anx7483_emul_data *anx7483 = emulator->data;
	const struct register_config *config = get_reg_config(reg);

	if (!config) {
		LOG_DBG("Unknown register %x", reg);
		return -EINVAL;
	}

	*val = anx7483->regs[reg];
	return 0;
}

int anx7483_emul_set_reg(const struct emul *emulator, int reg, uint8_t val)
{
	struct anx7483_emul_data *anx7483 = emulator->data;
	const struct register_config *config = get_reg_config(reg);

	if (!config) {
		LOG_DBG("Unknown register %x", reg);
		return -EINVAL;
	}

	if ((val & config->reserved) != (config->def & config->reserved)) {
		LOG_DBG("Reserved bits modified for reg %02x, val: %02x, \
			default: %02x, reserved: %02x",
			reg, val, config->def, config->reserved);
		return -EINVAL;
	}

	if (reg >= ARRAY_SIZE(anx7483->regs)) {
		LOG_DBG("Register %x is out of bounds", reg);
		return -EINVAL;
	}

	anx7483->regs[reg] = val;
	return 0;
}

void anx7483_emul_reset(const struct emul *emul)
{
	/* Use the setter helps catch any default misconfigs. */
	for (size_t i = 0; i < ARRAY_SIZE(register_configs); i++)
		anx7483_emul_set_reg(emul, register_configs[i].reg,
				     register_configs[i].def);
}

static int anx7483_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	struct anx7483_emul_data *data = emul->data;

	anx7483_emul_reset(emul);
	i2c_common_emul_init(&data->common);

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
