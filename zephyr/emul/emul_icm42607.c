/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/accelgyro_icm42607.h"
#include "emul/emul_icm42607.h"
#include "emul/emul_stub_device.h"
#include "queue.h"
#include "util.h"

#include <zephyr/device.h>

#define DT_DRV_COMPAT zephyr_icm42607_emul

#define REG_MAX 255

struct icm42607_data {
	struct i2c_common_emul_data common;
	uint8_t user_bank_0[REG_MAX + 1];
	uint8_t mreg1[REG_MAX + 1];
	uint8_t mreg2[REG_MAX + 1];
	struct queue fifo;
};

static const uint8_t user_bank_0_default_values[REG_MAX + 1] = {
	[ICM42607_REG_MCLK_RDY] = ICM42607_MCLK_RDY, /* always powered on */
	[ICM42607_REG_WHO_AM_I] = ICM42607_CHIP_ICM42607P,
	[ICM42607_REG_INTF_CONFIG0] = 0x30, /* big endian by default */
};
static const uint8_t mreg1_default_values[REG_MAX + 1] = {};
static const uint8_t mreg2_default_values[REG_MAX + 1] = {};

static void icm42607_emul_fifo_flush(const struct emul *emul)
{
	struct icm42607_data *data = emul->data;
	uint8_t unused;

	while (queue_remove_unit(&data->fifo, &unused) == 1)
		;
}

void icm42607_emul_reset(const struct emul *emul)
{
	struct icm42607_data *data = emul->data;

	memcpy(data->user_bank_0, user_bank_0_default_values, REG_MAX + 1);
	memcpy(data->mreg1, mreg1_default_values, REG_MAX + 1);
	memcpy(data->mreg2, mreg2_default_values, REG_MAX + 1);
	icm42607_emul_fifo_flush(emul);
}

int icm42607_emul_peek_reg(const struct emul *emul, int reg)
{
	struct icm42607_data *data = emul->data;
	uint8_t *regs = data->user_bank_0;

	if (!IN_RANGE(reg, 0, REG_MAX)) {
		return -1;
	}
	return regs[reg];
}

int icm42607_emul_write_reg(const struct emul *emul, int reg, int val)
{
	struct icm42607_data *data = emul->data;
	uint8_t *regs = data->user_bank_0;

	if (!IN_RANGE(reg, 0, REG_MAX)) {
		return -1;
	}

	regs[reg] = val;

	return 0;
}

void icm42607_emul_push_fifo(const struct emul *emul, const uint8_t *src,
			     int size)
{
	struct icm42607_data *data = emul->data;
	struct queue *fifo = &data->fifo;

	queue_add_units(fifo, src, size);
}

static int icm42607_emul_read(const struct emul *emul, int reg, uint8_t *val,
			      int bytes, void *unused_data)
{
	struct icm42607_data *data = emul->data;
	uint8_t *regs = data->user_bank_0;
	int pos = reg + bytes;

	if (!IN_RANGE(pos, 0, REG_MAX)) {
		return -1;
	}

	if (reg == ICM42607_REG_FIFO_COUNT) {
		int fifo_count = queue_count(&data->fifo);

		if (bytes == 0) {
			*val = fifo_count & 0xff;
		} else {
			*val = (fifo_count >> 8) & 0xff;
		}

		return bytes < 2 ? 0 : -1;
	}

	if (reg == ICM42607_REG_FIFO_DATA) {
		return queue_remove_unit(&data->fifo, val) == 1 ? 0 : -1;
	}

	if (pos == ICM42607_REG_M_R) {
		int block_sel = regs[ICM42607_REG_BLK_SEL_R];
		int mreg_addr = regs[ICM42607_REG_MADDR_R];

		if (!IN_RANGE(mreg_addr, 0, REG_MAX)) {
			return -1;
		}

		if (block_sel == 0) {
			*val = data->mreg1[mreg_addr];
			return 0;
		}
		if (block_sel == 0x28) {
			*val = data->mreg2[mreg_addr];
			return 0;
		}

		return -1;
	}

	*val = regs[pos];

	return 0;
}

static int icm42607_emul_write(const struct emul *emul, int reg, uint8_t val,
			       int bytes, void *unused_data)
{
	struct icm42607_data *data = emul->data;
	uint8_t *regs = data->user_bank_0;
	int pos = reg + bytes - 1;

	if (!IN_RANGE(pos, 0, REG_MAX) || !IN_RANGE(val, 0, UINT8_MAX)) {
		return -1;
	}

	if (reg == ICM42607_REG_SIGNAL_PATH_RESET) {
		if (bytes == 1 && (val & ICM42607_FIFO_FLUSH)) {
			icm42607_emul_fifo_flush(emul);
		}
		return 0;
	}

	if (pos == ICM42607_REG_M_W) {
		int block_sel = regs[ICM42607_REG_BLK_SEL_W];
		int mreg_addr = regs[ICM42607_REG_MADDR_W];

		if (!IN_RANGE(mreg_addr, 0, REG_MAX)) {
			return -1;
		}

		if (block_sel == 0) {
			data->mreg1[mreg_addr] = val;
			return 0;
		}
		if (block_sel == 0x28) {
			data->mreg2[mreg_addr] = val;
			return 0;
		}

		return -1;
	}

	regs[pos] = val;

	return 0;
}

static int icm42607_emul_init(const struct emul *emul,
			      const struct device *parent)
{
	struct icm42607_data *data = (struct icm42607_data *)emul->data;
	struct i2c_common_emul_data *common_data = &data->common;

	i2c_common_emul_init(common_data);
	i2c_common_emul_set_read_func(common_data, icm42607_emul_read, NULL);
	i2c_common_emul_set_write_func(common_data, icm42607_emul_write, NULL);

	icm42607_emul_reset(emul);

	return 0;
}

struct i2c_common_emul_data *
emul_icm42607_get_i2c_common_data(const struct emul *emul)
{
	struct icm42607_data *data = (struct icm42607_data *)emul->data;

	return &data->common;
}

#define INIT_ICM42607_EMUL(n)                                          \
	static struct i2c_common_emul_cfg common_cfg_##n;              \
	static struct icm42607_data icm42607_data_##n;                 \
	static struct i2c_common_emul_cfg common_cfg_##n = {           \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),        \
		.data = &icm42607_data_##n.common,                     \
		.addr = DT_INST_REG_ADDR(n)                            \
	};                                                             \
	static struct icm42607_data icm42607_data_##n = {              \
		.common = { .cfg = &common_cfg_##n },                  \
		.fifo = QUEUE_NULL(1024, uint8_t),                     \
	};                                                             \
	EMUL_DT_INST_DEFINE(n, icm42607_emul_init, &icm42607_data_##n, \
			    &common_cfg_##n, &i2c_common_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(INIT_ICM42607_EMUL)

DT_INST_FOREACH_STATUS_OKAY(EMUL_STUB_DEVICE);
