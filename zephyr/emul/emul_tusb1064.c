/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/usb_mux/tusb1064.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "util.h"

#include <zephyr/device.h>

#define DT_DRV_COMPAT zephyr_tusb1064_emul

#define TUSB1064_REG_MAX 255

struct tusb1064_data {
	struct i2c_common_emul_data common;
	uint8_t regs[TUSB1064_REG_MAX + 1];
};

static const uint8_t default_values[TUSB1064_REG_MAX + 1] = {
	[TUSB1064_REG_GENERAL] = 0x01,
	[TUSB1064_REG_DP1DP3EQ_SEL] = 0x00,
	[TUSB1064_REG_DP0DP2EQ_SEL] = 0x00,
};

void tusb1064_emul_reset_regs(const struct emul *emul)
{
	struct tusb1064_data *data = (struct tusb1064_data *)emul->data;

	memcpy(data->regs, default_values, TUSB1064_REG_MAX + 1);
}

int tusb1064_emul_peek_reg(const struct emul *emul, int reg)
{
	struct tusb1064_data *data = (struct tusb1064_data *)emul->data;
	uint8_t *regs = data->regs;

	if (!IN_RANGE(reg, 0, TUSB1064_REG_MAX)) {
		return -1;
	}
	return regs[reg];
}

static int tusb1064_emul_read(const struct emul *emul, int reg, uint8_t *val,
			      int bytes, void *unused_data)
{
	struct tusb1064_data *data = (struct tusb1064_data *)emul->data;
	uint8_t *regs = data->regs;
	int pos = reg + bytes;

	if (!IN_RANGE(pos, 0, TUSB1064_REG_MAX)) {
		return -1;
	}
	*val = regs[pos];

	return 0;
}

static int tusb1064_emul_write(const struct emul *emul, int reg, uint8_t val,
			       int bytes, void *unused_data)
{
	struct tusb1064_data *data = (struct tusb1064_data *)emul->data;
	uint8_t *regs = data->regs;
	int pos = reg + bytes - 1;

	if (!IN_RANGE(pos, 0, TUSB1064_REG_MAX) ||
	    !IN_RANGE(val, 0, UINT8_MAX)) {
		return -1;
	}
	regs[pos] = val;

	return 0;
}

static int tusb1064_emul_init(const struct emul *emul,
			      const struct device *parent)
{
	struct tusb1064_data *data = (struct tusb1064_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, tusb1064_emul_read, NULL);
	i2c_common_emul_set_write_func(common_data, tusb1064_emul_write, NULL);

	tusb1064_emul_reset_regs(emul);

	return 0;
}

#define INIT_TUSB1064_EMUL(n)                                          \
	static struct i2c_common_emul_cfg common_cfg_##n;              \
	static struct tusb1064_data tusb1064_data_##n;                 \
	static struct i2c_common_emul_cfg common_cfg_##n = {           \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),        \
		.data = &tusb1064_data_##n.common,                     \
		.addr = DT_INST_REG_ADDR(n)                            \
	};                                                             \
	static struct tusb1064_data tusb1064_data_##n = {              \
		.common = { .cfg = &common_cfg_##n }                   \
	};                                                             \
	EMUL_DT_INST_DEFINE(n, tusb1064_emul_init, &tusb1064_data_##n, \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_TUSB1064_EMUL)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
