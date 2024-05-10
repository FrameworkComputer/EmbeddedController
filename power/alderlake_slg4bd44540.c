/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This was originally copied form power/icelake.c (also used on TGL and
 * ADL) and adapted to support ADL designs using the Silergy SLG4BD44540
 * power sequencer chip.
 */

#include "board_config.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "power.h"
#include "power/alderlake_slg4bd44540.h"
#include "power/intel_x86.h"
#include "system_boot_time.h"
#include "timer.h"

/*
 * These delays are used by the brya power sequence reference design and
 * should be suitable for variants.
 */

/* PG_EC_ALL_SYS_PWRGD high to VCCST_PWRGD high delay */
#define VCCST_PWRGD_DELAY_MS 2

/* IMVP9_VRRDY high to PCH_PWROK high delay */
#define PCH_PWROK_DELAY_MS 2

/* PG_EC_ALL_SYS_PWRGD high to EC_PCH_SYS_PWROK high delay */
#define SYS_PWROK_DELAY_MS 45

/* IMVP9_VRRDY high timeout */
#define VRRDY_TIMEOUT_MS 50

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

#ifdef CONFIG_BRINGUP
#define GPIO_SET_LEVEL(signal, value) \
	gpio_set_level_verbose(CC_CHIPSET, signal, value)
#else
#define GPIO_SET_LEVEL(signal, value) gpio_set_level(signal, value)
#endif

/* The wait time is ~150 msec, allow for safety margin. */
#define IN_PCH_SLP_SUS_WAIT_TIME_USEC (250 * MSEC)

#define RSMRST_L_PGOOD_MASK POWER_SIGNAL_MASK(X86_RSMRST_L_PGOOD)
#define DSW_DPWROK_MASK POWER_SIGNAL_MASK(X86_DSW_DPWROK)
#define ALL_SYS_PGOOD_MASK POWER_SIGNAL_MASK(X86_ALL_SYS_PGOOD)

#ifndef CONFIG_POWER_SIGNAL_RUNTIME_CONFIG
const
#endif /* !CONFIG_POWER_SIGNAL_RUNTIME_CONFIG */
/* Power signals list. Must match order of enum power_signal. */
struct power_signal_info power_signal_list[] = {
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
};

BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

__overridable int board_get_all_sys_pgood(void)
{
	return !!(power_get_signals() & ALL_SYS_PGOOD_MASK);
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	CPRINTS("%s() %d", __func__, reason);
	report_ap_reset(reason);

	/* Turn off RMSRST_L  to meet tPCH12 */
	board_before_rsmrst(0);
	GPIO_SET_LEVEL(GPIO_PCH_RSMRST_L, 0);
	board_after_rsmrst(0);

	/* Turn off S5 rails */
	GPIO_SET_LEVEL(GPIO_EN_S5_RAILS, 0);

	/* Now wait for DSW_PWROK and RSMRST_ODL to go away. */
	if (power_wait_mask_signals_timeout(
		    0, DSW_DPWROK_MASK | RSMRST_L_PGOOD_MASK, 50 * MSEC) !=
	    EC_SUCCESS)
		CPRINTS("DSW_PWROK or RSMRST_ODL didn't go low! Assuming G3.");
}

void chipset_handle_espi_reset_assert(void)
{
	/* No special handling needed. */
}

enum power_state chipset_force_g3(void)
{
	chipset_force_shutdown(CHIPSET_SHUTDOWN_G3);

	return POWER_G3;
}

static void ap_off(void)
{
	GPIO_SET_LEVEL(GPIO_VCCST_PWRGD_OD, 0);
	GPIO_SET_LEVEL(GPIO_PCH_PWROK, 0);
	GPIO_SET_LEVEL(GPIO_EC_PCH_SYS_PWROK, 0);
}

/*
 * We have asserted VCCST_PWRGO_OD, now wait for the IMVP9.1
 * to assert IMVP9_VRRDY_OD.
 *
 * Returns state of VRRDY.
 */

static int wait_for_vrrdy(void)
{
	int timeout_ms = VRRDY_TIMEOUT_MS;
	int vrrdy;

	for (; timeout_ms > 0; --timeout_ms) {
		vrrdy = gpio_get_level(GPIO_IMVP9_VRRDY_OD);
		if (vrrdy != 0)
			return 1;
		crec_msleep(1);
	}
	return 0;
}

/*
 * The relationship between these signals is described in
 * Intel PDG #627205 rev. 0.81.
 *
 * tCPU16: >= 0
 *	VCCST_PWRGD to PCH_PWROK
 * tPLT05: >= 0
 *	SYS_ALL_PWRGD to SYS_PWROK
 *	PCH_PWROK to SYS_PWROK
 */

static void all_sys_pwrgd_pass_thru(void)
{
	int sys_pg;
	int vccst_pg;
	int pch_pok;
	int sys_pok;

	sys_pg = board_get_all_sys_pgood();

	if (IS_ENABLED(CONFIG_BRINGUP))
		CPRINTS("PG_EC_ALL_SYS_PWRGD is %d", sys_pg);

	if (sys_pg == 0) {
		ap_off();
		return;
	}

	/* PG_EC_ALL_SYS_PWRGD is asserted, enable VCCST_PWRGD_OD. */

	vccst_pg = gpio_get_level(GPIO_VCCST_PWRGD_OD);
	if (vccst_pg == 0) {
		crec_msleep(VCCST_PWRGD_DELAY_MS);
		GPIO_SET_LEVEL(GPIO_VCCST_PWRGD_OD, 1);
	}

	/* Enable PCH_PWROK, gated by VRRDY. */

	pch_pok = gpio_get_level(GPIO_PCH_PWROK);
	if (pch_pok == 0) {
		if (wait_for_vrrdy() == 0) {
			CPRINTS("Timed out waiting for VRRDY, "
				"shutting AP off!");
			ap_off();
			return;
		}
		crec_msleep(PCH_PWROK_DELAY_MS);
		GPIO_SET_LEVEL(GPIO_PCH_PWROK, 1);
	}

	/* Enable PCH_SYS_PWROK. */

	sys_pok = gpio_get_level(GPIO_EC_PCH_SYS_PWROK);
	if (sys_pok == 0) {
		crec_msleep(SYS_PWROK_DELAY_MS);
		/* Check if we lost power while waiting. */
		sys_pg = board_get_all_sys_pgood();
		if (sys_pg == 0) {
			CPRINTS("PG_EC_ALL_SYS_PWRGD deasserted, "
				"shutting AP off!");
			ap_off();
			return;
		}
		GPIO_SET_LEVEL(GPIO_EC_PCH_SYS_PWROK, 1);
		/* PCH will now release PLT_RST */
	}
}

enum power_state power_handle_state(enum power_state state)
{
	all_sys_pwrgd_pass_thru();

	common_intel_x86_handle_rsmrst(state);

	switch (state) {
	case POWER_G3S5:
		GPIO_SET_LEVEL(GPIO_EN_S5_RAILS, 1);

		update_ap_boot_time(ARAIL);

		if (power_wait_signals(IN_PGOOD_ALL_CORE))
			break;

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
		/* If SLP_SUS_L is asserted, we're no longer in S5. */
		if (!power_has_signals(IN_PCH_SLP_SUS_DEASSERTED))
			return POWER_S5G3;
		break;

	default:
		break;
	}

	return common_intel_x86_power_handle_state(state);
}
