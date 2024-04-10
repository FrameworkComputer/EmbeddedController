/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Cometlake chipset power control module for Chrome EC */

#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "power.h"
#include "power/intel_x86.h"
#include "power_button.h"
#include "task.h"
#include "timer.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

/* Power signals list. Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	[X86_SLP_S0_DEASSERTED] = {
		.gpio = GPIO_PCH_SLP_S0_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH |
			POWER_SIGNAL_DISABLE_AT_BOOT,
		.name = "SLP_S0_DEASSERTED",
	},
	[X86_SLP_S3_DEASSERTED] = {
		.gpio = SLP_S3_SIGNAL_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S3_DEASSERTED",
	},
	[X86_SLP_S4_DEASSERTED] = {
		.gpio = SLP_S4_SIGNAL_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S4_DEASSERTED",
	},
	[X86_RSMRST_L_PGOOD] = {
		.gpio = GPIO_PG_EC_RSMRST_ODL,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "RSMRST_L_PGOOD",
	},
	[X86_PP5000_A_PGOOD] = {
		.gpio = GPIO_PP5000_A_PG_OD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "PP5000_A_PGOOD",
	},
	[X86_ALL_SYS_PGOOD] = {
		.gpio = GPIO_PG_EC_ALL_SYS_PWRGD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "ALL_SYS_PWRGD",
	},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

static int forcing_shutdown; /* Forced shutdown in progress? */

/* Default no action, overwrite it in board.c if necessary*/
__overridable void board_chipset_forced_shutdown(void)
{
	return;
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	int timeout_ms = 50;

	CPRINTS("%s(%d)", __func__, reason);
	report_ap_reset(reason);

	/* Turn off RSMRST_L to meet tPCH12 */
	gpio_set_level(GPIO_PCH_RSMRST_L, 0);

	/* Turn off A (except PP5000_A) rails*/
	gpio_set_level(GPIO_EN_A_RAILS, 0);

#ifdef CONFIG_POWER_PP5000_CONTROL
	/* Issue a request to turn off the rail. */
	power_5v_enable(task_get_current(), 0);
#else
	/* Turn off PP5000_A rail */
	gpio_set_level(GPIO_EN_PP5000_A, 0);
#endif

	/* For b:143440730, stop checking GPIO_ALL_SYS_PGOOD if system is
	 * already force to G3.
	 */
	board_chipset_forced_shutdown();

	/* Need to wait a min of 10 msec before check for power good */
	crec_msleep(10);

	/* Now wait for PP5000_A and RSMRST_L to go low */
	while ((gpio_get_level(GPIO_PP5000_A_PG_OD) ||
		power_has_signals(IN_PGOOD_ALL_CORE)) &&
	       (timeout_ms > 0)) {
		crec_msleep(1);
		timeout_ms--;
	};

	if (!timeout_ms)
		CPRINTS("PP5000_A rail still up!  Assuming G3.");
}

void chipset_handle_espi_reset_assert(void)
{
	/*
	 * If eSPI_Reset# pin is asserted without SLP_SUS# being asserted, then
	 * it means that there is an unexpected power loss (global reset
	 * event). In this case, check if shutdown was being forced by pressing
	 * power button. If yes, release power button.
	 */
	if ((power_get_signals() & IN_PGOOD_ALL_CORE) && forcing_shutdown) {
		power_button_pch_release();
		forcing_shutdown = 0;
	}
}

enum power_state chipset_force_g3(void)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);

	return POWER_G3;
}

/* Default no action, overwrite it in board.c if necessary*/
__attribute__((weak)) void all_sys_pgood_check_reboot(void)
{
	return;
}

/* Called by APL power state machine when transitioning from G3 to S5 */
void chipset_pre_init_callback(void)
{
	/* Enable 5.0V and 3.3V rails, and wait for Power Good */
#ifdef CONFIG_POWER_PP5000_CONTROL
	power_5v_enable(task_get_current(), 1);
#else
	/* Turn on PP5000_A rail */
	gpio_set_level(GPIO_EN_PP5000_A, 1);
#endif
	/* Turn on A (except PP5000_A) rails*/
	gpio_set_level(GPIO_EN_A_RAILS, 1);

	/*
	 * The status of the 5000_A rail is verifed in the calling function via
	 * power_wait_signals() as PP5000_A_PGOOD is included in the
	 * CHIPSET_G3S5_POWERUP_SIGNAL macro.
	 */

	/* For b:143440730, system might hang-up before enter S0/S3. Check
	 * GPIO_ALL_SYS_PGOOD here to make sure it will trigger every time.
	 */
	all_sys_pgood_check_reboot();
}

enum power_state power_handle_state(enum power_state state)
{
	int all_sys_pwrgd_in;
	int all_sys_pwrgd_out;

	/*
	 * Check if RSMRST_L signal state has changed and if so, pass the new
	 * value along to the PCH. However, if the new transition of RSMRST_L
	 * from the Sielgo is from low to high, then gate this transition to the
	 * AP by the PP5000_A rail. If the new transition is from high to low,
	 * then pass that through regardless of the PP5000_A value.
	 *
	 * The PP5000_A power good signal will float high if the
	 * regulator is not powered, so checking both that the EN and the PG
	 * signals are high.
	 */
	if ((gpio_get_level(GPIO_PP5000_A_PG_OD) &&
	     gpio_get_level(GPIO_EN_PP5000_A)) ||
	    gpio_get_level(GPIO_PCH_RSMRST_L))
		common_intel_x86_handle_rsmrst(state);

	switch (state) {
	case POWER_S5:
		if (forcing_shutdown) {
			power_button_pch_release();
			forcing_shutdown = 0;
		}
		/* If RSMRST_L is asserted, we're no longer in S5. */
		if (!power_has_signals(IN_PGOOD_ALL_CORE))
			return POWER_S5G3;
		break;

	case POWER_S0:
		/*
		 * Check value of PG_EC_ALL_SYS_PWRGD to see if PCH_SYS_PWROK
		 * needs to be changed. If it's low->high transition, requires a
		 * 2msec delay.
		 */
		all_sys_pwrgd_in = gpio_get_level(GPIO_PG_EC_ALL_SYS_PWRGD);
		all_sys_pwrgd_out = gpio_get_level(GPIO_PCH_SYS_PWROK);

		if (all_sys_pwrgd_in != all_sys_pwrgd_out) {
			if (all_sys_pwrgd_in)
				crec_msleep(2);
			gpio_set_level(GPIO_PCH_SYS_PWROK, all_sys_pwrgd_in);
		}
		break;

	default:
		break;
	}

	return common_intel_x86_power_handle_state(state);
}
