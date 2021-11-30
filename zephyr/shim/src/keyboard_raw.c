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

BUILD_ASSERT(DT_HAS_CHOSEN(cros_ec_raw_kb),
	     "a cros-ec,raw-kb device must be chosen");
#define cros_kb_raw_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_raw_kb))

/**
 * Initialize the raw keyboard interface.
 */
void keyboard_raw_init(void)
{
	if (!device_is_ready(cros_kb_raw_dev))
		k_oops();

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
	cros_kb_raw_drive_column(cros_kb_raw_dev, col);
}

/**
 * Read raw row state.
 * Bits are 1 if signal is present, 0 if not present.
 */
test_mockable int keyboard_raw_read_rows(void)
{
	return cros_kb_raw_read_rows(cros_kb_raw_dev);
}

/**
 * Enable or disable keyboard interrupts.
 */
void keyboard_raw_enable_interrupt(int enable)
{
	cros_kb_raw_enable_interrupt(cros_kb_raw_dev, enable);
}
