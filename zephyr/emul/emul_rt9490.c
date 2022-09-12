/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>

#include "driver/charger/rt9490.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_rt9490.h"
#include "emul/emul_stub_device.h"
#include "util.h"

#define DT_DRV_COMPAT zephyr_rt9490_emul

#define RT9490_REG_MAX 255

struct rt9490_data {
	struct i2c_common_emul_data common;
	uint8_t regs[RT9490_REG_MAX + 1];
};

static const uint8_t default_values[RT9490_REG_MAX + 1] = {
	[RT9490_REG_SAFETY_TMR_CTRL] = 0x3D,
	[RT9490_REG_ADD_CTRL0] = 0x76,
};

void rt9490_emul_reset_regs(const struct emul *emul)
{
	struct rt9490_data *data = emul->data;

	memcpy(data->regs, default_values, RT9490_REG_MAX + 1);
}

int rt9490_emul_peek_reg(const struct emul *emul, int reg)
{
	struct rt9490_data *data = emul->data;
	uint8_t *regs = data->regs;

	if (!IN_RANGE(reg, 0, RT9490_REG_MAX)) {
		return -1;
	}
	return regs[reg];
}

static int rt9490_emul_read(const struct emul *emul, int reg, uint8_t *val,
			    int bytes, void *unused_data)
{
	struct rt9490_data *data = emul->data;
	uint8_t *regs = data->regs;

	if (!IN_RANGE(reg, 0, RT9490_REG_MAX)) {
		return -1;
	}
	*val = regs[reg];

	return 0;
}

static int rt9490_emul_write(const struct emul *emul, int reg, uint8_t val,
			     int bytes, void *unused_data)
{
	struct rt9490_data *data = emul->data;
	uint8_t *regs = data->regs;

	if (!IN_RANGE(reg, 0, RT9490_REG_MAX) || !IN_RANGE(val, 0, UINT8_MAX)) {
		return -1;
	}
	regs[reg] = val;

	return 0;
}

static int rt9490_emul_init(const struct emul *emul,
			    const struct device *parent)
{
	struct rt9490_data *data = (struct rt9490_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, rt9490_emul_read, NULL);
	i2c_common_emul_set_write_func(common_data, rt9490_emul_write, NULL);

	rt9490_emul_reset_regs(emul);

	return 0;
}

#define INIT_RT9490_EMUL(n)                                        \
	static struct i2c_common_emul_cfg common_cfg_##n;          \
	static struct rt9490_data rt9490_data_##n;                 \
	static struct i2c_common_emul_cfg common_cfg_##n = {       \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),    \
		.data = &rt9490_data_##n.common,                   \
		.addr = DT_INST_REG_ADDR(n)                        \
	};                                                         \
	static struct rt9490_data rt9490_data_##n = {              \
		.common = { .cfg = &common_cfg_##n }               \
	};                                                         \
	EMUL_DT_INST_DEFINE(n, rt9490_emul_init, &rt9490_data_##n, \
			    &common_cfg_##n, &i2c_common_emul_api)

DT_INST_FOREACH_STATUS_OKAY(INIT_RT9490_EMUL)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
