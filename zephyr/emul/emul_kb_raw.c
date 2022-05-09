/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_kb_raw_emul

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emul_kb_raw);

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <drivers/cros_kb_raw.h>
#include <keyboard_raw.h>

struct kb_raw_emul_data {
	int active_column;
	int *matrix;
};

struct kb_raw_emul_cfg {
	/** Label of the I2C device being emulated */
	const char *dev_label;
	/** Pointer to run-time data */
	struct kb_raw_emul_data *data;
	/** Number of emulated keyboard rows. */
	int rows;
	/** Number of emulated keyboard columns. */
	int cols;
};

/**
 * @brief Set up a new kn_raw emulator
 *
 * @param device Device node.
 *
 * @return 0 indicating success (always)
 */
static int kb_raw_emul_init(const struct device *dev)
{
	const struct kb_raw_emul_cfg *cfg = dev->config;
	struct kb_raw_emul_data *data = dev->data;

	memset(data->matrix, 0, sizeof(int) * cfg->cols);
	return 0;
}

static int emul_kb_raw_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static int emul_kb_raw_enable_interrupt(const struct device *dev, int enable)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(enable);

	return 0;
}

static int emul_kb_raw_read_rows(const struct device *dev)
{
	struct kb_raw_emul_data *data = dev->data;

	if (data->active_column == KEYBOARD_COLUMN_ALL)
		return 0;
	if (data->active_column == KEYBOARD_COLUMN_NONE)
		return 0;
	return data->matrix[data->active_column];
}

static int emul_kb_raw_drive_column(const struct device *dev, int col)
{
	const struct kb_raw_emul_cfg *cfg = dev->config;
	struct kb_raw_emul_data *data = dev->data;

	if (col >= cfg->cols)
		return -EINVAL;
	data->active_column = col;
	return 0;
}

int emul_kb_raw_set_kbstate(const struct device *dev, uint8_t row, uint8_t col,
			    int pressed)
{
	const struct kb_raw_emul_cfg *cfg = dev->config;
	struct kb_raw_emul_data *data = dev->data;

	if (col >= cfg->cols || row >= cfg->rows)
		return -EINVAL;
	if (pressed)
		data->matrix[col] |= 1 << row;
	else
		data->matrix[col] &= ~(1 << row);
	return 0;
}

static const struct cros_kb_raw_driver_api emul_kb_raw_driver_api = {
	.init = emul_kb_raw_init,
	.drive_colum = emul_kb_raw_drive_column,
	.read_rows = emul_kb_raw_read_rows,
	.enable_interrupt = emul_kb_raw_enable_interrupt,
};

#define KB_RAW_EMUL(n)                                                     \
	static int kb_raw_emul_matrix_##n[DT_INST_PROP(n, cols)];          \
	static struct kb_raw_emul_data kb_raw_emul_data_##n = {            \
		.matrix = kb_raw_emul_matrix_##n,                          \
	};                                                                 \
									   \
	static const struct kb_raw_emul_cfg kb_raw_emul_cfg_##n = {        \
		.dev_label = DT_INST_LABEL(n),                             \
		.data = &kb_raw_emul_data_##n,                             \
		.rows = DT_INST_PROP(n, rows),                             \
		.cols = DT_INST_PROP(n, cols),                             \
	};                                                                 \
	DEVICE_DT_INST_DEFINE(n, kb_raw_emul_init, NULL,                   \
			      &kb_raw_emul_data_##n, &kb_raw_emul_cfg_##n, \
			      PRE_KERNEL_1,                                \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,         \
			      &emul_kb_raw_driver_api)
DT_INST_FOREACH_STATUS_OKAY(KB_RAW_EMUL);
