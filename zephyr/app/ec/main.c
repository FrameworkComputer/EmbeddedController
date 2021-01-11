/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/printk.h>
#include <zephyr.h>

#include "ec_tasks.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "zephyr_espi_shim.h"

void main(void)
{
	printk("Hello from a Chrome EC!\n");
	printk("  BOARD=%s\n", CONFIG_BOARD);
	printk("  ACTIVE_COPY=%s\n", CONFIG_CROS_EC_ACTIVE_COPY);

	if (IS_ENABLED(HAS_TASK_KEYSCAN)) {
		keyboard_scan_init();
	}

	if (IS_ENABLED(CONFIG_PLATFORM_EC_ESPI)) {
		if (zephyr_shim_setup_espi() < 0) {
			printk("Failed to init eSPI!\n");
		}
	}

	/* Call init hooks before main tasks start */
	if (IS_ENABLED(CONFIG_PLATFORM_EC_HOOKS)) {
		hook_notify(HOOK_INIT);
	}

	/* Start the EC tasks after performing all main initialization */
	if (IS_ENABLED(CONFIG_SHIMMED_TASKS)) {
		start_ec_tasks();
	}
}
