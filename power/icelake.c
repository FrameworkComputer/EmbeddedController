/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Icelake chipset power control module for Chrome EC */

#include "board_config.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "power.h"
#include "power/intel_x86.h"
#include "power_button.h"
#include "system_boot_time.h"
#include "task.h"
#include "timer.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

#ifdef CONFIG_BRINGUP
#define GPIO_SET_LEVEL(signal, value) \
	gpio_set_level_verbose(CC_CHIPSET, signal, value)
#else
#define GPIO_SET_LEVEL(signal, value) gpio_set_level(signal, value)
#endif

#define RSMRST_L_PGOOD_MASK POWER_SIGNAL_MASK(X86_RSMRST_L_PGOOD)
#define DSW_DPWROK_MASK POWER_SIGNAL_MASK(X86_DSW_DPWROK)
#define ALL_SYS_PGOOD_MASK POWER_SIGNAL_MASK(X86_ALL_SYS_PGOOD)

/* The wait time is ~150 msec, allow for safety margin. */
#define IN_PCH_SLP_SUS_WAIT_TIME_USEC (250 * MSEC)

static int forcing_shutdown; /* Forced shutdown in progress? */

/* Power signals list. Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	[X86_SLP_S0_DEASSERTED] = {
		.gpio = GPIO_PCH_SLP_S0_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH |
			POWER_SIGNAL_DISABLE_AT_BOOT,
		.name = "SLP_S0_DEASSERTED",
	},
	[X86_SLP_S3_DEASSERTED] = {
		.gpio = (enum gpio_signal)SLP_S3_SIGNAL_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S3_DEASSERTED",
	},
	[X86_SLP_S4_DEASSERTED] = {
		.gpio = (enum gpio_signal)SLP_S4_SIGNAL_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S4_DEASSERTED",
	},
	[X86_SLP_S5_DEASSERTED] = {
		.gpio = (enum gpio_signal)SLP_S5_SIGNAL_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_S5_DEASSERTED",
	},
	[X86_SLP_SUS_DEASSERTED] = {
		.gpio = GPIO_SLP_SUS_L,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "SLP_SUS_DEASSERTED",
	},
	[X86_RSMRST_L_PGOOD] = {
		.gpio = GPIO_PG_EC_RSMRST_ODL,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "RSMRST_L_PGOOD",
	},
	[X86_DSW_DPWROK] = {
		.gpio = GPIO_PG_EC_DSW_PWROK,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "DSW_DPWROK",
	},
	[X86_ALL_SYS_PGOOD] = {
		.gpio = GPIO_PG_EC_ALL_SYS_PWRGD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "ALL_SYS_PWRGD",
	},
#ifdef CONFIG_CHIPSET_JASPERLAKE
	[PP1050_ST_PGOOD] = {
		.gpio = GPIO_PG_PP1050_ST_OD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "PP1050_ST_PGOOD",
	},
	[DRAM_PGOOD] = {
		.gpio = GPIO_PG_DRAM_OD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "DRAM_PGOOD",
	},
	[VCCIO_EXT_PGOOD] = {
		.gpio = GPIO_PG_VCCIO_EXT_OD,
		.flags = POWER_SIGNAL_ACTIVE_HIGH,
		.name = "VCCIO_EXT_PGOOD",
	},
#endif
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s() %d", __func__, reason);
	report_ap_reset(reason);

	/* Turn off RMSRST_L  to meet tPCH12 */
	board_before_rsmrst(0);
	GPIO_SET_LEVEL(GPIO_PCH_RSMRST_L, 0);
	board_after_rsmrst(0);

	/* Turn off DSW_PWROK to meet tPCH14 */
	GPIO_SET_LEVEL(GPIO_PCH_DSW_PWROK, 0);

	/* Turn off DSW load switch. */
	GPIO_SET_LEVEL(GPIO_EN_PP3300_A, 0);

	/*
	 * For JSL, we need to wait 60ms before turning off PP5000_U to allow
	 * VCCIN_AUX time to discharge.
	 */
	if (IS_ENABLED(CONFIG_CHIPSET_JASPERLAKE))
		msleep(60);

	/* Turn off PP5000 rail */
	if (IS_ENABLED(CONFIG_POWER_PP5000_CONTROL))
		power_5v_enable(task_get_current(), 0);
	else
		GPIO_SET_LEVEL(GPIO_EN_PP5000, 0);

	/*
	 * For JSL, we may not catch the DSW power good transitioning if this
	 * occurs in suspend as our ADC interrupts are disabled for power
	 * reasons. Therefore, kick the chipset state machine in order to catch
	 * up with the current state of affairs.
	 */
	if (IS_ENABLED(CONFIG_CHIPSET_JASPERLAKE))
		power_signal_interrupt(GPIO_PG_EC_DSW_PWROK);

	/* Now wait for DSW_PWROK and RSMRST_ODL to go away. */
	if (power_wait_mask_signals_timeout(
		    0, DSW_DPWROK_MASK | RSMRST_L_PGOOD_MASK, 50 * MSEC) !=
	    EC_SUCCESS)
		CPRINTS("DSW_PWROK or RSMRST_ODL didn't go low! Assuming G3.");
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

enum power_state chipset_force_g3(void)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);

	return POWER_G3;
}

static void enable_pp5000_rail(void)
{
	if (IS_ENABLED(CONFIG_POWER_PP5000_CONTROL))
		power_5v_enable(task_get_current(), 1);
	else
		GPIO_SET_LEVEL(GPIO_EN_PP5000, 1);
}

static void dsw_pwrok_pass_thru(void)
{
	int dswpwrok_in = !!(power_get_signals() & DSW_DPWROK_MASK);

	/* Pass-through DSW_PWROK to ICL. */
	if (dswpwrok_in != gpio_get_level(GPIO_PCH_DSW_PWROK)) {
#if defined(CONFIG_CHIPSET_SLP_S3_L_OVERRIDE)
		if (dswpwrok_in) {
			/*
			 * Once DSW_PWROK is high, reconfigure SLP_S3_L back to
			 * an input after a short delay.
			 */
			msleep(1);
			CPRINTS("Release SLP_S3_L");
			gpio_reset(SLP_S3_SIGNAL_L);
			power_signal_enable_interrupt(SLP_S3_SIGNAL_L);
		}
#endif

		CPRINTS("Pass thru GPIO_DSW_PWROK: %d", dswpwrok_in);
		/*
		 * A minimum 10 msec delay is required between PP3300_A being
		 * stable and the DSW_PWROK signal being passed to the PCH.
		 */
		msleep(10);
		GPIO_SET_LEVEL(GPIO_PCH_DSW_PWROK, dswpwrok_in);
	}
}

/*
 * Set the PWROK signal state
 *
 * &param level		0 deasserts the signal, other values assert the signal
 */
static void pwrok_signal_set(const struct intel_x86_pwrok_signal *signal,
			     int level)
{
	GPIO_SET_LEVEL(signal->gpio, signal->active_low ? !level : level);
}

/*
 * Pass through the state of the ALL_SYS_PWRGD input to all the PWROK outputs
 * defined by the board.
 */
static void all_sys_pwrgd_pass_thru(void)
{
	int all_sys_pwrgd_in = !!(power_get_signals() & ALL_SYS_PGOOD_MASK);
	const struct intel_x86_pwrok_signal *pwrok_signal;
	int signal_count;
	int i;

	if (all_sys_pwrgd_in) {
		pwrok_signal = pwrok_signal_assert_list;
		signal_count = pwrok_signal_assert_count;
	} else {
		pwrok_signal = pwrok_signal_deassert_list;
		signal_count = pwrok_signal_deassert_count;
	}

	/*
	 * Loop through all PWROK signals defined by the board and set
	 * to match the current ALL_SYS_PWRGD input.
	 */
	for (i = 0; i < signal_count; i++, pwrok_signal++) {
		if (pwrok_signal->delay_ms > 0)
			msleep(pwrok_signal->delay_ms);

		pwrok_signal_set(pwrok_signal, all_sys_pwrgd_in);
	}
}

enum power_state power_handle_state(enum power_state state)
{
	dsw_pwrok_pass_thru();

	all_sys_pwrgd_pass_thru();

	common_intel_x86_handle_rsmrst(state);

	switch (state) {
	case POWER_G3S5:
		if (intel_x86_wait_power_up_ok() != EC_SUCCESS) {
			chipset_force_shutdown(
				CHIPSET_SHUTDOWN_BATTERY_INHIBIT);
			return POWER_G3;
		}
#if defined(CONFIG_CHIPSET_SLP_S3_L_OVERRIDE)
		/*
		 * Prevent glitches on the SLP_S3_L and PCH_PWROK
		 * signals while when the PP3300_A rail is turned on.
		 * Drive SLP_S3_L from the EC until DSW_PWROK is high.
		 */
		CPRINTS("Drive SLP_S3_L low during PP3300_A rampup");
		power_signal_disable_interrupt(SLP_S3_SIGNAL_L);
		gpio_set_flags(SLP_S3_SIGNAL_L, GPIO_ODR_LOW);
#endif

		/* Default behavior - turn on PP5000 rail first */
		if (!IS_ENABLED(CONFIG_CHIPSET_PP3300_RAIL_FIRST))
			enable_pp5000_rail();

		/*
		 * TODO(b/111121615): Should modify this to wait until the
		 * common power state machine indicates that it's ok to try an
		 * boot the AP prior to turning on the 3300_A rail. This could
		 * be done using chipset_pre_init_callback()
		 */
		/* Turn on the PP3300_DSW rail. */
		GPIO_SET_LEVEL(GPIO_EN_PP3300_A, 1);

		update_ap_boot_time(ARAIL);

		if (power_wait_signals(IN_PGOOD_ALL_CORE))
			break;

		/* Pass thru DSWPWROK again since we changed it. */
		dsw_pwrok_pass_thru();

		/* Turn on PP5000 after PP3300 and DSW PWROK when enabled */
		if (IS_ENABLED(CONFIG_CHIPSET_PP3300_RAIL_FIRST))
			enable_pp5000_rail();

		/*
		 * Now wait for SLP_SUS_L to go high based on tPCH32. If this
		 * signal doesn't go high within 250 msec then go back to G3.
		 */
		if (power_wait_signals_timeout(IN_PCH_SLP_SUS_DEASSERTED,
					       IN_PCH_SLP_SUS_WAIT_TIME_USEC) !=
		    EC_SUCCESS) {
			CPRINTS("SLP_SUS_L didn't go high!  Going back to G3.");
			return POWER_S5G3;
		}
		break;

	case POWER_S5:
		if (forcing_shutdown) {
			power_button_pch_release();
			forcing_shutdown = 0;
		}
		/* If SLP_SUS_L is asserted, we're no longer in S5. */
		if (!power_has_signals(IN_PCH_SLP_SUS_DEASSERTED))
			return POWER_S5G3;
		break;

#ifdef CONFIG_CHIPSET_JASPERLAKE
	case POWER_S3S0:
		GPIO_SET_LEVEL(GPIO_EN_VCCIO_EXT, 1);
		/* Now wait for ALL_SYS_PWRGD. */
		if (power_wait_signals_timeout(ALL_SYS_PGOOD_MASK, 10 * MSEC) !=
		    EC_SUCCESS)
			CPRINTS("ALL_SYS_PWRGD not received.");
		break;

	case POWER_S0S3:
		GPIO_SET_LEVEL(GPIO_EN_VCCIO_EXT, 0);
		break;
#endif /* CONFIG_CHIPSET_JASPERLAKE */

	default:
		break;
	}

	return common_intel_x86_power_handle_state(state);
}
