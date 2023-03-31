/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/ppc/nx20p348x.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "usbc/ppc_nx20p348x.h"
#include "usbc_ppc.h"
#include "util.h"

#include <zephyr/device.h>
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
	uint8_t regs[NX20P348X_MAX_REG + 1];
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

void nx20p348x_emul_reset_regs(const struct emul *emul)
{
	struct nx20p348x_emul_data *data =
		(struct nx20p348x_emul_data *)emul->data;

	memset(data->regs, 0, sizeof(data->regs));

	for (int i = 0; i < ARRAY_SIZE(nx20p348x_defaults); i++) {
		struct nx20p348x_reg_default def = nx20p348x_defaults[i];

		data->regs[def.offset] = def.val;
	}
}

uint8_t nx20p348x_emul_peek(const struct emul *emul, int reg)
{
	__ASSERT_NO_MSG(IN_RANGE(reg, 0, NX20P348X_MAX_REG));

	struct nx20p348x_emul_data *data =
		(struct nx20p348x_emul_data *)emul->data;

	return data->regs[reg];
}

static int nx20p348x_emul_read(const struct emul *emul, int reg, uint8_t *val,
			       int bytes, void *unused_data)
{
	struct nx20p348x_emul_data *data =
		(struct nx20p348x_emul_data *)emul->data;

	if (!IN_RANGE(reg, 0, NX20P348X_MAX_REG)) {
		LOG_ERR("Register out of range: %d", reg);
		return -EINVAL;
	}

	if (bytes != 0) {
		LOG_ERR("Emulator expects single byte transactions: "
			"%d bytes requested",
			bytes);
		return -EINVAL;
	}

	*val = data->regs[reg];

	return 0;
}

static int nx20p348x_emul_write(const struct emul *emul, int reg, uint8_t val,
				int bytes, void *unused_data)
{
	struct nx20p348x_emul_data *data =
		(struct nx20p348x_emul_data *)emul->data;

	if (!IN_RANGE(reg, 0, NX20P348X_MAX_REG)) {
		LOG_ERR("Register out of range: %d", reg);
		return -EINVAL;
	}

	if (bytes != 1) {
		LOG_ERR("Emulator expects single byte transactions: "
			"%d bytes written",
			bytes);
		return -EINVAL;
	}

	data->regs[reg] = val;

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

#define INIT_NX20P348X_EMUL(n)                                                \
	static struct i2c_common_emul_cfg common_cfg_##n;                     \
	static struct nx20p348x_emul_data nx20p348x_emul_data_##n;            \
	static struct i2c_common_emul_cfg common_cfg_##n = {                  \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),               \
		.data = &nx20p348x_emul_data_##n.common,                      \
		.addr = DT_INST_REG_ADDR(n)                                   \
	};                                                                    \
	EMUL_DT_INST_DEFINE(n, nx20p348x_emul_init, &nx20p348x_emul_data_##n, \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_NX20P348X_EMUL)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

static void nx20p348x_emul_reset_rule_before(const struct ztest_unit_test *test,
					     void *data)
{
	ARG_UNUSED(test);
	ARG_UNUSED(data);

#define NX20P348X_EMUL_RESET_RULE_BEFORE(n) \
	nx20p348x_emul_reset_regs(EMUL_DT_GET(DT_DRV_INST(n)))

	DT_INST_FOREACH_STATUS_OKAY(NX20P348X_EMUL_RESET_RULE_BEFORE);
}
ZTEST_RULE(nx20p348x_emul_reset, nx20p348x_emul_reset_rule_before, NULL);
