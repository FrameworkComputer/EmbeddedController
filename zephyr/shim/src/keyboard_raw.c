/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Functions needed by keyboard scanner module for Chrome EC */

#include <device.h>
#include <logging/log.h>
#include <soc.h>
#include <zephyr.h>

#include "drivers/cros_kb_raw.h"
#include "keyboard_raw.h"

LOG_MODULE_REGISTER(shim_cros_kb_raw, LOG_LEVEL_ERR);

#define CROS_KB_RAW_NODE DT_NODELABEL(cros_kb_raw)
static const struct device *cros_kb_raw_dev;

/**
 * Initialize the raw keyboard interface.
 */
void keyboard_raw_init(void)
{
	cros_kb_raw_dev = DEVICE_DT_GET(CROS_KB_RAW_NODE);
	if (!device_is_ready(cros_kb_raw_dev)) {
		LOG_ERR("Error: device %s is not ready", cros_kb_raw_dev->name);
		return;
	}

	LOG_INF("%s", __func__);
	cros_kb_raw_init(cros_kb_raw_dev);
}

/**
 * Finish initialization after task scheduling has started.
 */
void keyboard_raw_task_start(void)
{
	keyboard_raw_enable_interrupt(1);
}

/**
 * Drive the specified column low.
 */
test_mockable void keyboard_raw_drive_column(int col)
{
	if (cros_kb_raw_dev)
		cros_kb_raw_drive_column(cros_kb_raw_dev, col);
	else
		LOG_ERR("%s: no cros_kb_raw device!", __func__);
}

/**
 * Read raw row state.
 * Bits are 1 if signal is present, 0 if not present.
 */
test_mockable int keyboard_raw_read_rows(void)
{
	if (cros_kb_raw_dev)
		return cros_kb_raw_read_rows(cros_kb_raw_dev);

	LOG_ERR("%s: no cros_kb_raw device!", __func__);
	return -EIO;
}

/**
 * Enable or disable keyboard interrupts.
 */
void keyboard_raw_enable_interrupt(int enable)
{
	if (cros_kb_raw_dev)
		cros_kb_raw_enable_interrupt(cros_kb_raw_dev, enable);
	else
		LOG_ERR("%s: no cros_kb_raw device!", __func__);
}
