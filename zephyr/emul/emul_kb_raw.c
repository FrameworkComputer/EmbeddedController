/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/logging/log.h>

#include <drivers/cros_kb_raw.h>
#include <keyboard_raw.h>

#define DT_DRV_COMPAT cros_ec_kb_raw_emul

LOG_MODULE_REGISTER(emul_kb_raw);

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

static const struct device *emul_dev;

/**
 * @brief Set up a new kb_raw emulator
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
	emul_dev = dev;
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

void emul_kb_raw_reset(const struct device *dev)
{
	const struct kb_raw_emul_cfg *cfg = dev->config;
	struct kb_raw_emul_data *data = dev->data;

	for (int col = 0; col < cfg->cols; col++) {
		data->matrix[col] = 0;
	}
}

/* Global keyboard raw API implementations */
void keyboard_raw_init(void)
{
	/* Do nothing. kb_raw_emul_init is called at boot time. */
}

void keyboard_raw_drive_column(int col)
{
	if (emul_dev) {
		emul_kb_raw_drive_column(emul_dev, col);
	}
}

int keyboard_raw_read_rows(void)
{
	if (emul_dev) {
		return emul_kb_raw_read_rows(emul_dev);
	}
	return 0;
}

void keyboard_raw_enable_interrupt(int enable)
{
	if (emul_dev) {
		emul_kb_raw_enable_interrupt(emul_dev, enable);
	}
}

void keyboard_raw_task_start(void)
{
	keyboard_raw_enable_interrupt(1);
}

#define KB_RAW_EMUL(n)                                                     \
	static int kb_raw_emul_matrix_##n[DT_INST_PROP(n, cols)];          \
	static struct kb_raw_emul_data kb_raw_emul_data_##n = {            \
		.matrix = kb_raw_emul_matrix_##n,                          \
	};                                                                 \
                                                                           \
	static const struct kb_raw_emul_cfg kb_raw_emul_cfg_##n = {        \
		.dev_label = DT_NODE_FULL_NAME(DT_DRV_INST(n)),            \
		.data = &kb_raw_emul_data_##n,                             \
		.rows = DT_INST_PROP(n, rows),                             \
		.cols = DT_INST_PROP(n, cols),                             \
	};                                                                 \
	DEVICE_DT_INST_DEFINE(n, kb_raw_emul_init, NULL,                   \
			      &kb_raw_emul_data_##n, &kb_raw_emul_cfg_##n, \
			      PRE_KERNEL_1,                                \
			      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL)
DT_INST_FOREACH_STATUS_OKAY(KB_RAW_EMUL);
