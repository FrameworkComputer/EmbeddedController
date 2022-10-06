/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Skylake IMVP8 / ROP PMIC chipset power control module for Chrome EC */

#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "lpc.h"
#include "panic.h"
#include "power/intel_x86.h"
#include "power_button.h"
#include "system.h"
#include "timer.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

static int forcing_shutdown; /* Forced shutdown in progress? */

/* Power signals list. Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
#ifdef CONFIG_POWER_S0IX
	[X86_SLP_S0_DEASSERTED] = {
		GPIO_PCH_SLP_S0_L,
		POWER_SIGNAL_ACTIVE_HIGH | POWER_SIGNAL_DISABLE_AT_BOOT,
		"SLP_S0_DEASSERTED",
	},
#endif
	[X86_SLP_S3_DEASSERTED] = {
		(enum gpio_signal)SLP_S3_SIGNAL_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_S3_DEASSERTED",
	},
	[X86_SLP_S4_DEASSERTED] = {
		(enum gpio_signal)SLP_S4_SIGNAL_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_S4_DEASSERTED",
	},
	[X86_SLP_SUS_DEASSERTED] = {
		GPIO_PCH_SLP_SUS_L,
		POWER_SIGNAL_ACTIVE_HIGH,
		"SLP_SUS_DEASSERTED",
	},
	[X86_RSMRST_L_PWRGD] = {
		GPIO_PG_EC_RSMRST_ODL,
		POWER_SIGNAL_ACTIVE_HIGH,
		"RSMRST_N_PWRGD",
	},
	[X86_PMIC_DPWROK] = {
		GPIO_PMIC_DPWROK,
		POWER_SIGNAL_ACTIVE_HIGH,
		"PMIC_DPWROK",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s()", __func__);

	/*
	 * Force off. Sending a reset command to the PMIC will power off
	 * the EC, so simulate a long power button press instead. This
	 * condition will reset once the state machine transitions to G3.
	 * Consider reducing the latency here by changing the power off
	 * hold time on the PMIC.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		report_ap_reset(reason);
		forcing_shutdown = 1;
		power_button_pch_press();
	}
}

__attribute__((weak)) void chipset_set_pmic_slp_sus_l(int level)
{
	gpio_set_level(GPIO_PMIC_SLP_SUS_L, level);
}

enum power_state chipset_force_g3(void)
{
	CPRINTS("Forcing fake G3.");

	chipset_set_pmic_slp_sus_l(0);

	return POWER_G3;
}

static void handle_slp_sus(enum power_state state)
{
	/* If we're down or going down don't do anythin with SLP_SUS_L. */
	if (state == POWER_G3 || state == POWER_S5G3)
		return;

	/* Always mimic PCH SLP_SUS request for all other states. */
	chipset_set_pmic_slp_sus_l(gpio_get_level(GPIO_PCH_SLP_SUS_L));
}

void chipset_handle_espi_reset_assert(void)
{
	/*
	 * If eSPI_Reset# pin is asserted without SLP_SUS# being asserted, then
	 * it means that there is an unexpected power loss (global reset
	 * event). In this case, check if shutdown was being forced by pressing
	 * power button. If yes, release power button.
	 */
	if ((power_get_signals() & IN_PCH_SLP_SUS_DEASSERTED) &&
	    forcing_shutdown) {
		power_button_pch_release();
		forcing_shutdown = 0;
	}
}

enum power_state power_handle_state(enum power_state state)
{
	enum power_state new_state;

	/* Process RSMRST_L state changes. */
	common_intel_x86_handle_rsmrst(state);

	if (state == POWER_S5 && forcing_shutdown) {
		power_button_pch_release();
		forcing_shutdown = 0;
	}

	new_state = common_intel_x86_power_handle_state(state);

	/* Process SLP_SUS_L state changes after a new state is decided. */
	handle_slp_sus(new_state);

	return new_state;
}

/* Workaround for flags getting lost with power cycle */
__attribute__((weak)) int board_has_working_reset_flags(void)
{
	return 1;
}

#ifdef CONFIG_CHIPSET_HAS_PLATFORM_PMIC_RESET
void chipset_handle_reboot(void)
{
	int flags;

	if (system_jumped_to_this_image())
		return;

	/* Interrogate current reset flags from previous reboot. */
	flags = system_get_reset_flags();

	/*
	 * Do not make PMIC re-sequence the power rails if the following reset
	 * conditions are not met.
	 */
	if (!(flags & (EC_RESET_FLAG_WATCHDOG | EC_RESET_FLAG_SOFT |
		       EC_RESET_FLAG_HARD)))
		return;

	/* Preserve AP off request. */
	if (flags & EC_RESET_FLAG_AP_OFF) {
		/* Do not issue PMIC reset if board cannot save reset flags */
		if (!board_has_working_reset_flags()) {
			ccprintf("Skip PMIC reset due to board issue.\n");
			cflush();
			return;
		}
		chip_save_reset_flags(EC_RESET_FLAG_AP_OFF);
	}

#ifdef CONFIG_CHIP_PANIC_BACKUP
	/* Ensure panic data if any is backed up. */
	chip_panic_data_backup();
#endif

	ccprintf("Restarting system with PMIC.\n");
	/* Flush console */
	cflush();

	/* Bring down all rails but RTC rail (including EC power). */
	gpio_set_level(GPIO_EC_PLATFORM_RST, 1);
	while (1)
		; /* wait here */
}
#if !defined(CONFIG_VBOOT_EFS) || !defined(CONFIG_VBOOT_EFS2)
/* This is run in main for EFS1 & EFS2 */
DECLARE_HOOK(HOOK_INIT, chipset_handle_reboot, HOOK_PRIO_FIRST);
#endif
#endif /* CONFIG_CHIPSET_HAS_PLATFORM_RESET */
