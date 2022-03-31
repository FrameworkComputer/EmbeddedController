/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <kernel.h>
#include <sys/printk.h>
#include <shell/shell_uart.h>
#include <zephyr.h>

#include "ap_power/ap_pwrseq.h"
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
#include "ec_app_main.h"

/* For testing purposes this is not named main. See main_shim.c for the real
 * main() function.
 */
void ec_app_main(void)
{
	system_common_pre_init();

	/*
	 * Initialize reset logs. This needs to be done before any updates of
	 * reset logs because we need to verify if the values remain the same
	 * after every EC reset.
	 */
	if (IS_ENABLED(CONFIG_CMD_AP_RESET_LOG)) {
		init_reset_log();
	}

	system_print_banner();

	if (IS_ENABLED(CONFIG_PLATFORM_EC_WATCHDOG) &&
		!IS_ENABLED(CONFIG_WDT_DISABLE_AT_BOOT)) {
		watchdog_init();
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

	if (IS_ENABLED(CONFIG_PLATFORM_EC_VBOOT_EFS2)) {
		/*
		 * For RO, it behaves as follows:
		 *   In recovery, it enables PD communication and returns.
		 *   In normal boot, it verifies and jumps to RW.
		 * For RW, it returns immediately.
		 */
		vboot_main();
	}

	/*
	 * Hooks run from the system workqueue and must be the lowest priority
	 * thread. By default, the system workqueue is run at the lowest
	 * cooperative thread priority, blocking all preemptive threads until
	 * the deferred work is completed.
	 */
	k_thread_priority_set(&k_sys_work_q.thread, LOWEST_THREAD_PRIORITY);

	/*
	 * Print the init time.  Not completely accurate because it can't take
	 * into account the time before timer_init(), but it'll at least catch
	 * the majority of the time.
	 */
	cprints(CC_SYSTEM, "Inits done");

	/* Start the EC tasks after performing all main initialization */
	if (IS_ENABLED(CONFIG_SHIMMED_TASKS)) {
		start_ec_tasks();
	}
	if (IS_ENABLED(CONFIG_AP_PWRSEQ)) {
		ap_pwrseq_task_start();
	}
}
