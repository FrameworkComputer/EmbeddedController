/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Raw keyboard I/O layer for emulator */

#include "common.h"
#include "keyboard_config.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "task.h"
#include "util.h"

test_mockable void keyboard_raw_init(void)
{
	/* Nothing */
}

test_mockable void keyboard_raw_task_start(void)
{
	/* Nothing */
}

test_mockable void keyboard_raw_drive_column(int out)
{
	/* Nothing */
}

test_mockable int keyboard_raw_read_rows(void)
{
	/* Nothing pressed */
	return 0;
}

test_mockable void keyboard_raw_enable_interrupt(int enable)
{
	/* Nothing */
}

test_mockable void keyboard_raw_gpio_interrupt(enum gpio_signal signal)
{
#ifdef HAS_TASK_KEYSCAN
	task_wake(TASK_ID_KEYSCAN);
#endif
}
