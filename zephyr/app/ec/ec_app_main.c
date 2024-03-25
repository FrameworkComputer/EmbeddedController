/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power_interface.h"
#include "button.h"
#include "chipset.h"
#include "cros_board_info.h"
#include "ec_app_main.h"
#include "ec_tasks.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "lpc.h"
#include "system.h"
#include "usbc/pd_task_intel_altmode.h"
#include "vboot.h"
#include "watchdog.h"
#include "zephyr_espi_shim.h"

#include <zephyr/kernel.h>
#include <zephyr/pm/policy.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/sys/printk.h>

static struct k_timer no_sleep_boot_timer;
static void boot_allow_sleep(struct k_timer *timer)
{
	pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
}

/* For testing purposes this is not named main. See main_shim.c for the real
 * main() function.
 */
void ec_app_main(void)
{
	/*
	 * Initialize reset logs. This needs to be done before any updates of
	 * reset logs because we need to verify if the values remain the same
	 * after every EC reset.
	 */
	if (IS_ENABLED(CONFIG_CMD_AP_RESET_LOG)) {
		init_reset_log();
	}

	system_print_banner();

	if (IS_ENABLED(CONFIG_WATCHDOG) &&
	    !IS_ENABLED(CONFIG_WDT_DISABLE_AT_BOOT)) {
		watchdog_init();
	}

	if (IS_ENABLED(CONFIG_PLATFORM_EC_BOOT_NO_SLEEP)) {
		k_timeout_t duration =
			K_MSEC(CONFIG_PLATFORM_EC_BOOT_NO_SLEEP_MS);

		k_timer_init(&no_sleep_boot_timer, boot_allow_sleep, NULL);
		k_timer_start(&no_sleep_boot_timer, duration, K_NO_WAIT);

		pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_IDLE,
					 PM_ALL_SUBSTATES);
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

	/*
	 * Copy this block in case you need even earlier hooks instead of moving
	 * it. Callbacks of this type are expected to handle multiple calls.
	 */
	if (IS_ENABLED(CONFIG_PLATFORM_EC_HOOKS)) {
		hook_notify(HOOK_INIT_EARLY);
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

#ifdef CONFIG_AP_PWRSEQ_DRIVER
	/*
	 * Some components query AP power state during initialization, AP power
	 * sequence driver thread needs to be started earlier in order to
	 * determine current AP power state.
	 */
	if (IS_ENABLED(CONFIG_AP_PWRSEQ)) {
		ap_pwrseq_task_start();
	}
#endif

	/* Call init hooks before main tasks start */
	if (IS_ENABLED(CONFIG_PLATFORM_EC_HOOKS)) {
		hook_notify(HOOK_INIT);
	}

	/*
	 * If the EC has exclusive control over the CBI EEPROM WP signal, have
	 * the EC set the WP if appropriate.  Note that once the WP is set, the
	 * EC must be reset via EC_RST_ODL in order for the WP to become unset.
	 */
	if (IS_ENABLED(CONFIG_PLATFORM_EC_EEPROM_CBI_WP) && system_is_locked())
		cbi_latch_eeprom_wp();

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

#ifndef CONFIG_AP_PWRSEQ_DRIVER
	if (IS_ENABLED(CONFIG_AP_PWRSEQ)) {
		ap_pwrseq_task_start();
	}
#endif

	if (IS_ENABLED(CONFIG_USB_PD_ALTMODE_INTEL)) {
		intel_altmode_task_start();
	}
}
