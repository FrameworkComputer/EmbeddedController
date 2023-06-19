/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger/chg_isl9241.h"
#include "driver/charger/isl9241.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "util.h"

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT ISL9241_CHG_COMPAT

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(emul_isl9241);

/* Device ID sits at the end of the register space (0xFF) */
#define ISL9241_MAX_REG ISL9241_REG_DEVICE_ID

struct isl9241_emul_data {
	struct i2c_common_emul_data common;
	uint16_t regs[ISL9241_MAX_REG + 1];
};

/* Note: registers are all 2 bytes */
struct isl9241_reg_default {
	uint8_t offset;
	uint16_t val;
};

/* Chip defaults for non-zero registers (spec Rev 5.0, Table 1) */
struct isl9241_reg_default isl9241_defaults[] = {
	/* Note: 3s default here */
	{ .offset = ISL9241_REG_MAX_SYSTEM_VOLTAGE, .val = 0x3120 },
	{ .offset = ISL9241_REG_ADAPTER_CUR_LIMIT2, .val = 0x05DC },
	{ .offset = ISL9241_REG_CONTROL1, .val = 0x0103 },
	{ .offset = ISL9241_REG_CONTROL2, .val = 0x6000 },
	{ .offset = ISL9241_REG_ADAPTER_CUR_LIMIT1, .val = 0x05DC },
	{ .offset = ISL9241_REG_CONTROL6, .val = 0x1FFF },
	{ .offset = ISL9241_REG_AC_PROCHOT, .val = 0x0C00 },
	{ .offset = ISL9241_REG_DC_PROCHOT, .val = 0x1000 },
	{ .offset = ISL9241_REG_OTG_VOLTAGE, .val = 0x0D08 },
	{ .offset = ISL9241_REG_OTG_CURRENT, .val = 0x0200 },
	{ .offset = ISL9241_REG_VIN_VOLTAGE, .val = 0x0C00 },
	{ .offset = ISL9241_REG_CONTROL3, .val = 0x0300 },
	{ .offset = ISL9241_REG_MANUFACTURER_ID, .val = 0x0049 },
	{ .offset = ISL9241_REG_DEVICE_ID, .val = 0x000E },
};

void isl9241_emul_reset_regs(const struct emul *emul)
{
	struct isl9241_emul_data *data = (struct isl9241_emul_data *)emul->data;

	memset(data->regs, 0, sizeof(data->regs));

	for (int i = 0; i < ARRAY_SIZE(isl9241_defaults); i++) {
		struct isl9241_reg_default def = isl9241_defaults[i];

		data->regs[def.offset] = def.val;
	}
}

uint16_t isl9241_emul_peek(const struct emul *emul, int reg)
{
	__ASSERT_NO_MSG(IN_RANGE(reg, 0, ISL9241_MAX_REG));

	struct isl9241_emul_data *data = (struct isl9241_emul_data *)emul->data;

	return data->regs[reg];
}

void isl9241_emul_set_vbus(const struct emul *emul, int vbus_mv)
{
	struct isl9241_emul_data *data = (struct isl9241_emul_data *)emul->data;
	uint16_t adc_reg;

	if (vbus_mv > 0)
		data->regs[ISL9241_REG_INFORMATION2] |=
			ISL9241_INFORMATION2_ACOK_PIN;
	else
		data->regs[ISL9241_REG_INFORMATION2] &=
			~ISL9241_INFORMATION2_ACOK_PIN;

	adc_reg = vbus_mv / ISL9241_VIN_ADC_STEP_MV;
	adc_reg <<= ISL9241_VIN_ADC_BIT_OFFSET;
	data->regs[ISL9241_REG_VIN_ADC_RESULTS] = adc_reg;
}

void isl9241_emul_set_vsys(const struct emul *emul, int vsys_mv)
{
	struct isl9241_emul_data *data = (struct isl9241_emul_data *)emul->data;
	uint16_t adc_reg;

	adc_reg = vsys_mv / ISL9241_VIN_ADC_STEP_MV;
	adc_reg <<= ISL9241_VIN_ADC_BIT_OFFSET;
	data->regs[ISL9241_REG_VSYS_ADC_RESULTS] = adc_reg;
}

static int isl9241_emul_read(const struct emul *emul, int reg, uint8_t *val,
			     int bytes, void *unused_data)
{
	struct isl9241_emul_data *data = (struct isl9241_emul_data *)emul->data;

	if (!IN_RANGE(reg, 0, ISL9241_MAX_REG))
		return -EINVAL;

	if (!IN_RANGE(bytes, 0, 1))
		return -EINVAL;

	if (bytes == 0)
		*val = (uint8_t)(data->regs[reg] & 0xFF);
	else
		*val = (uint8_t)((data->regs[reg] >> 8) & 0xFF);

	return 0;
}

static int isl9241_emul_write(const struct emul *emul, int reg, uint8_t val,
			      int bytes, void *unused_data)
{
	struct isl9241_emul_data *data = (struct isl9241_emul_data *)emul->data;

	if (!IN_RANGE(reg, 0, ISL9241_MAX_REG))
		return -EINVAL;

	if (!IN_RANGE(bytes, 1, 2))
		return -EINVAL;

	if (bytes == 1)
		data->regs[reg] = val & 0xFF;
	else
		data->regs[reg] |= val << 8;

	return 0;
}

static int isl9241_emul_init(const struct emul *emul,
			     const struct device *parent)
{
	struct isl9241_emul_data *data = (struct isl9241_emul_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, isl9241_emul_read, NULL);
	i2c_common_emul_set_write_func(common_data, isl9241_emul_write, NULL);

	isl9241_emul_reset_regs(emul);

	return 0;
}

#define INIT_ISL9241_EMUL(n)                                              \
	static struct i2c_common_emul_cfg common_cfg_##n;                 \
	static struct isl9241_emul_data isl9241_emul_data_##n;            \
	static struct i2c_common_emul_cfg common_cfg_##n = {              \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),           \
		.data = &isl9241_emul_data_##n.common,                    \
		.addr = DT_INST_REG_ADDR(n)                               \
	};                                                                \
	EMUL_DT_INST_DEFINE(n, isl9241_emul_init, &isl9241_emul_data_##n, \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_ISL9241_EMUL)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

static void isl9241_emul_reset_rule_before(const struct ztest_unit_test *test,
					   void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

#define ISL9241_EMUL_RESET_RULE_BEFORE(n) \
	isl9241_emul_reset_regs(EMUL_DT_GET(DT_DRV_INST(n)))

	DT_INST_FOREACH_STATUS_OKAY(ISL9241_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(isl9241_emul_reset, isl9241_emul_reset_rule_before, NULL);
