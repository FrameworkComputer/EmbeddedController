/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/usb_mux/ps8743.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_stub_device.h"
#include "util.h"

#include <zephyr/device.h>

#define DT_DRV_COMPAT zephyr_ps8743_emul

#define PS8743_REG_MAX 255

struct ps8743_data {
	struct i2c_common_emul_data common;
	uint8_t regs[PS8743_REG_MAX + 1];
};

static const uint8_t default_values[PS8743_REG_MAX + 1] = {
	[PS8743_REG_USB_EQ_RX] = 0x00,	  [PS8743_REG_REVISION_ID1] = 0x01,
	[PS8743_REG_REVISION_ID2] = 0x0b, [PS8743_REG_CHIP_ID1] = 0x41,
	[PS8743_REG_CHIP_ID2] = 0x87,
};

void ps8743_emul_reset_regs(const struct emul *emul)
{
	struct ps8743_data *data = (struct ps8743_data *)emul->data;

	memcpy(data->regs, default_values, PS8743_REG_MAX + 1);
}

int ps8743_emul_peek_reg(const struct emul *emul, int reg)
{
	struct ps8743_data *data = (struct ps8743_data *)emul->data;
	uint8_t *regs = data->regs;

	if (!IN_RANGE(reg, 0, PS8743_REG_MAX)) {
		return -1;
	}
	return regs[reg];
}

void ps8743_emul_set_reg(const struct emul *emul, int reg, int val)
{
	struct ps8743_data *data = (struct ps8743_data *)emul->data;
	uint8_t *regs = data->regs;

	if (!IN_RANGE(reg, 0, PS8743_REG_MAX)) {
		return;
	}

	regs[reg] = val;
}

static int ps8743_emul_read(const struct emul *emul, int reg, uint8_t *val,
			    int bytes, void *unused_data)
{
	struct ps8743_data *data = (struct ps8743_data *)emul->data;
	uint8_t *regs = data->regs;
	int pos = reg + bytes;

	if (!IN_RANGE(pos, 0, PS8743_REG_MAX)) {
		return -1;
	}
	*val = regs[pos];

	return 0;
}

static int ps8743_emul_write(const struct emul *emul, int reg, uint8_t val,
			     int bytes, void *unused_data)
{
	struct ps8743_data *data = (struct ps8743_data *)emul->data;
	uint8_t *regs = data->regs;
	int pos = reg + bytes - 1;

	if (!IN_RANGE(pos, 0, PS8743_REG_MAX) || !IN_RANGE(val, 0, UINT8_MAX)) {
		return -1;
	}
	regs[pos] = val;

	return 0;
}

static int ps8743_emul_init(const struct emul *emul,
			    const struct device *parent)
{
	struct ps8743_data *data = (struct ps8743_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, ps8743_emul_read, NULL);
	i2c_common_emul_set_write_func(common_data, ps8743_emul_write, NULL);

	ps8743_emul_reset_regs(emul);

	return 0;
}

#define INIT_PS8743_EMUL(n)                                       \
	static struct i2c_common_emul_cfg common_cfg_##n;         \
	static struct ps8743_data ps8743_data##n;                 \
	static struct i2c_common_emul_cfg common_cfg_##n = {      \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),   \
		.data = &ps8743_data##n.common,                   \
		.addr = DT_INST_REG_ADDR(n)                       \
	};                                                        \
	static struct ps8743_data ps8743_data##n = {              \
		.common = { .cfg = &common_cfg_##n }              \
	};                                                        \
	EMUL_DT_INST_DEFINE(n, ps8743_emul_init, &ps8743_data##n, \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_PS8743_EMUL)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);

struct i2c_common_emul_data *ps8743_get_i2c_common_data(const struct emul *emul)
{
	return emul->data;
}
