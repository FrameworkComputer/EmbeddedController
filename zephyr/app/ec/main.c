/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/printk.h>
#include <zephyr.h>

#include "button.h"
#include "chipset.h"
#include "ec_tasks.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lpc.h"
#include "system.h"
#include "vboot.h"
#include "watchdog.h"
#include "zephyr_espi_shim.h"

void main(void)
{
	printk("Hello from a Chrome EC!\n");
	printk("  BOARD=%s\n", CONFIG_BOARD);
	printk("  ACTIVE_COPY=%s\n", CONFIG_CROS_EC_ACTIVE_COPY);

	system_common_pre_init();

	/*
	 * Initialize reset logs. This needs to be done before any updates of
	 * reset logs because we need to verify if the values remain the same
	 * after every EC reset.
	 */
	if (IS_ENABLED(CONFIG_CMD_AP_RESET_LOG)) {
		init_reset_log();
	}

	/*
	 * Keyboard scan init/Button init can set recovery events to
	 * indicate to host entry into recovery mode. Before this is
	 * done, LPC_HOST_EVENT_ALWAYS_REPORT mask needs to be initialized
	 * correctly.
	 */
	if (IS_ENABLED(CONFIG_HOSTCMD_X86)) {
		lpc_init_mask();
	}

	if (IS_ENABLED(HAS_TASK_KEYSCAN)) {
		keyboard_scan_init();
	}

	if (IS_ENABLED(CONFIG_DEDICATED_RECOVERY_BUTTON) ||
	    IS_ENABLED(CONFIG_VOLUME_BUTTONS)) {
		button_init();
	}

	if (IS_ENABLED(CONFIG_PLATFORM_EC_ESPI)) {
		if (zephyr_shim_setup_espi() < 0) {
			printk("Failed to init eSPI!\n");
		}
	}

	if (IS_ENABLED(CONFIG_PLATFORM_EC_WATCHDOG)) {
		watchdog_init();
	}

	if (IS_ENABLED(CONFIG_PLATFORM_EC_VBOOT)) {
		/*
		 * For RO, it behaves as follows:
		 *   In recovery, it enables PD communication and returns.
		 *   In normal boot, it verifies and jumps to RW.
		 * For RW, it returns immediately.
		 */
		vboot_main();
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
