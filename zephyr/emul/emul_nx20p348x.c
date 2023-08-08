/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/ppc/nx20p348x.h"
#include "driver/tcpm/tcpci.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_nx20p348x.h"
#include "emul/emul_stub_device.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/utils.h"
#include "usbc/ppc_nx20p348x.h"
#include "usbc_ppc.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT NX20P348X_COMPAT

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(emul_nx20p348x);

/*
 * Device control reg marks the end of defined regs for the NX20P3483 (0x0B)
 */
#define NX20P348X_MAX_REG NX20P348X_DEVICE_CONTROL_REG

struct nx20p348x_emul_data {
	struct i2c_common_emul_data common;
	struct gpio_dt_spec irq_gpio;
	const struct emul *tcpc_emul;
	uint8_t regs[NX20P348X_MAX_REG + 1];
	bool tcpc_interact;
};

struct nx20p348x_reg_default {
	uint8_t offset;
	uint8_t val;
};

/* Chip defaults for non-zero registers (spec Rev 0.4 Table 9) */
struct nx20p348x_reg_default nx20p348x_defaults[] = {
	{ .offset = NX20P348X_DEVICE_ID_REG, .val = 0x09 },
	{ .offset = NX20P348X_OVLO_THRESHOLD_REG, .val = 0x01 },
	{ .offset = NX20P348X_HV_SRC_OCP_THRESHOLD_REG, .val = 0x0B },
	{ .offset = NX20P348X_5V_SRC_OCP_THRESHOLD_REG, .val = 0x0B },
};

static void nx20p348x_emul_interrupt_set(const struct emul *emul, int val)
{
	struct nx20p348x_emul_data *data =
		(struct nx20p348x_emul_data *)emul->data;

	int res = gpio_emul_input_set(data->irq_gpio.port, data->irq_gpio.pin,
				      val);
	__ASSERT_NO_MSG(res == 0);
}

void nx20p348x_emul_reset_regs(const struct emul *emul)
{
	struct nx20p348x_emul_data *data =
		(struct nx20p348x_emul_data *)emul->data;

	memset(data->regs, 0, sizeof(data->regs));

	for (int i = 0; i < ARRAY_SIZE(nx20p348x_defaults); i++) {
		struct nx20p348x_reg_default def = nx20p348x_defaults[i];

		data->regs[def.offset] = def.val;
	}
	nx20p348x_emul_interrupt_set(emul, 1);
	nx20p348x_emul_set_tcpc_interact(emul, true);
}

uint8_t nx20p348x_emul_peek(const struct emul *emul, int reg)
{
	__ASSERT_NO_MSG(IN_RANGE(reg, 0, NX20P348X_MAX_REG));

	struct nx20p348x_emul_data *data =
		(struct nx20p348x_emul_data *)emul->data;

	return data->regs[reg];
}

void nx20p348x_emul_set_tcpc_interact(const struct emul *emul, bool en)
{
	struct nx20p348x_emul_data *data =
		(struct nx20p348x_emul_data *)emul->data;
	data->tcpc_interact = en;
}

void nx20p348x_emul_set_interrupt1(const struct emul *emul, uint8_t val)
{
	struct nx20p348x_emul_data *data =
		(struct nx20p348x_emul_data *)emul->data;

	data->regs[NX20P348X_INTERRUPT1_REG] = val;

	nx20p348x_emul_interrupt_set(emul, 0);
}

static int nx20p348x_emul_read(const struct emul *emul, int reg, uint8_t *val,
			       int bytes, void *unused_data)
{
	struct nx20p348x_emul_data *data =
		(struct nx20p348x_emul_data *)emul->data;

	if (!IN_RANGE(reg, 0, NX20P348X_MAX_REG))
		return -EINVAL;

	if (bytes != 0)
		return -EINVAL;

	if (IS_ENABLED(CONFIG_PLATFORM_EC_USBC_PPC_NX20P3483) &&
	    data->tcpc_interact) {
		uint16_t pwr_status;
		bool src_en, snk_en;

		tcpci_emul_get_reg(data->tcpc_emul, TCPC_REG_POWER_STATUS,
				   &pwr_status);
		src_en = pwr_status & TCPC_REG_POWER_STATUS_SOURCING_VBUS;
		snk_en = pwr_status & TCPC_REG_POWER_STATUS_SINKING_VBUS;

		if (reg == NX20P348X_SWITCH_STATUS_REG) {
			if (src_en) {
				data->regs[reg] |=
					NX20P348X_SWITCH_STATUS_5VSRC;
			} else {
				data->regs[reg] &=
					~(NX20P348X_SWITCH_STATUS_5VSRC |
					  NX20P348X_SWITCH_STATUS_HVSRC);
			}
			if (snk_en) {
				data->regs[reg] |=
					NX20P348X_SWITCH_STATUS_HVSNK;
			} else {
				data->regs[reg] &=
					(~NX20P348X_SWITCH_STATUS_HVSNK);
			}
		} else if (reg == NX20P348X_DEVICE_STATUS_REG) {
			int mode = data->regs[reg] &
				   (~NX20P3483_DEVICE_MODE_MASK);
			bool db_exit =
				!!(data->regs[NX20P348X_DEVICE_CONTROL_REG] &
				   NX20P348X_CTRL_DB_EXIT);

			if (snk_en) {
				mode = NX20P3483_MODE_HV_SNK;
			} else if (src_en) {
				mode = NX20P3483_MODE_5V_SRC;
			} else if (data->regs[NX20P348X_SWITCH_STATUS_REG] &
				   NX20P348X_SWITCH_STATUS_HVSRC) {
				mode = NX20P3483_MODE_HV_SRC;
			} else if (!db_exit) {
				mode = NX20P348X_MODE_DEAD_BATTERY;
			}
			data->regs[reg] = (data->regs[reg] &
					   ~NX20P3483_DEVICE_MODE_MASK) |
					  mode;
		}
	}

	*val = data->regs[reg];

	/* Interrupt registers are clear on read and de-assert when serviced */
	if (reg == NX20P348X_INTERRUPT1_REG ||
	    reg == NX20P348X_INTERRUPT2_REG) {
		data->regs[reg] = 0;

		if (data->regs[NX20P348X_INTERRUPT1_REG] == 0 &&
		    data->regs[NX20P348X_INTERRUPT2_REG] == 0)
			nx20p348x_emul_interrupt_set(emul, 1);
	}

	return 0;
}

static int nx20p348x_emul_write(const struct emul *emul, int reg, uint8_t val,
				int bytes, void *unused_data)
{
	struct nx20p348x_emul_data *data =
		(struct nx20p348x_emul_data *)emul->data;

	if (!IN_RANGE(reg, 0, NX20P348X_MAX_REG))
		return -EINVAL;

	if (bytes != 1)
		return -EINVAL;

	data->regs[reg] = val;

	if (IS_ENABLED(CONFIG_PLATFORM_EC_USBC_PPC_NX20P3481) &&
	    reg == NX20P348X_SWITCH_CONTROL_REG) {
		bool enabled = val & NX20P3481_SWITCH_CONTROL_HVSNK;

		/* Update our status as if we turned on/off Vbus sinking */
		if (enabled)
			data->regs[NX20P348X_SWITCH_STATUS_REG] |=
				NX20P348X_SWITCH_STATUS_HVSNK;
		else
			data->regs[NX20P348X_SWITCH_STATUS_REG] &=
				~NX20P348X_SWITCH_STATUS_HVSNK;

		/* Do the same for sourcing */
		enabled = val & NX20P3481_SWITCH_CONTROL_5VSRC;
		if (enabled)
			data->regs[NX20P348X_SWITCH_STATUS_REG] |=
				NX20P348X_SWITCH_STATUS_5VSRC;
		else
			data->regs[NX20P348X_SWITCH_STATUS_REG] &=
				~NX20P348X_SWITCH_STATUS_5VSRC;
	}

	return 0;
}

static int nx20p348x_emul_init(const struct emul *emul,
			       const struct device *parent)
{
	struct nx20p348x_emul_data *data =
		(struct nx20p348x_emul_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, nx20p348x_emul_read, NULL);
	i2c_common_emul_set_write_func(common_data, nx20p348x_emul_write, NULL);

	nx20p348x_emul_reset_regs(emul);

	return 0;
}

#define INIT_NX20P348X_EMUL(n)                                                 \
	static struct nx20p348x_emul_data nx20p348x_emul_data_##n;             \
	static struct i2c_common_emul_cfg common_cfg_##n = {                   \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),                \
		.data = &nx20p348x_emul_data_##n.common,                       \
		.addr = DT_INST_REG_ADDR(n)                                    \
	};                                                                     \
	static struct nx20p348x_emul_data nx20p348x_emul_data_##n = {          \
		.irq_gpio = GPIO_DT_SPEC_INST_GET_OR(n, irq_gpios, {}),        \
		.tcpc_emul =                                                   \
			EMUL_GET_USBC_PROP_BINDING(ppc, DT_DRV_INST(n), tcpc), \
		.tcpc_interact = true,                                         \
	};                                                                     \
	EMUL_DT_INST_DEFINE(n, nx20p348x_emul_init, &nx20p348x_emul_data_##n,  \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_NX20P348X_EMUL)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

static void nx20p348x_emul_reset_rule_before(const struct ztest_unit_test *test,
					     void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

#define NX20P348X_EMUL_RESET_RULE_BEFORE(n) \
	nx20p348x_emul_reset_regs(EMUL_DT_GET(DT_DRV_INST(n)));

	DT_INST_FOREACH_STATUS_OKAY(NX20P348X_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(nx20p348x_emul_reset, nx20p348x_emul_reset_rule_before, NULL);
