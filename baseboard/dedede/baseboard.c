/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Dedede family-specific configuration */

#include "adc.h"
#include "board_config.h"
#include "chipset.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "intel_x86.h"

/******************************************************************************/
/*
 * PWROK signal configuration, see the PWROK Generation Flow Diagram in the
 * Jasper Lake Platform Design Guide for the list of potential signals.
 *
 * Dedede boards use this PWROK sequence:
 *	GPIO_ALL_SYS_PWRGD - turns on VCCIN rail
 *	GPIO_EC_AP_VCCST_PWRGD_OD - asserts VCCST_PWRGD to AP, requires 2ms
 *		delay from VCCST stable to meet the tCPU00 platform sequencing
 *		timing
 *	GPIO_EC_AP_PCH_PWROK_OD - asserts PMC_PCH_PWROK to the AP. Note that
 *		PMC_PCH_PWROK is also gated by the IMVP9_VRRDY_OD output from
 *		the VCCIN voltage rail controller.
 *	GPIO_EC_AP_SYS_PWROK - asserts PMC_SYS_PWROK to the AP
 *
 * Both PMC_PCH_PWROK and PMC_SYS_PWROK signals must both be asserted before
 * the Jasper Lake SoC deasserts PMC_RLTRST_N. The platform may deassert
 * PMC_PCH_PWROK and PMC_SYS_PWROK in any order to optimize overall boot
 * latency.
 */
const struct intel_x86_pwrok_signal pwrok_signal_assert_list[] = {
	{
		.gpio = GPIO_ALL_SYS_PWRGD,
	},
	{
		.gpio = GPIO_EC_AP_VCCST_PWRGD_OD,
		.delay_ms = 2,
	},
	{
		.gpio = GPIO_EC_AP_PCH_PWROK_OD,
	},
	{
		.gpio = GPIO_EC_AP_SYS_PWROK,
	},
};
const int pwrok_signal_assert_count = ARRAY_SIZE(pwrok_signal_assert_list);

const struct intel_x86_pwrok_signal pwrok_signal_deassert_list[] = {
	/* No delays needed during S0 exit */
	{
		.gpio = GPIO_EC_AP_VCCST_PWRGD_OD,
	},
	{
		.gpio = GPIO_EC_AP_PCH_PWROK_OD,
	},
	{
		.gpio = GPIO_EC_AP_SYS_PWROK,
	},
	/* Turn off the VCCIN rail last */
	{
		.gpio = GPIO_ALL_SYS_PWRGD,
	},
};
const int pwrok_signal_deassert_count = ARRAY_SIZE(pwrok_signal_deassert_list);


/*
 * Dedede does not use hibernate wake pins, but the super low power "Z-state"
 * instead in which the EC is powered off entirely.  Power will be restored to
 * the EC once one of the wake up events occurs.  These events are ACOK, lid
 * open, and a power button press.
 */
const enum gpio_signal hibernate_wake_pins[] = {};
const int hibernate_wake_pins_used;

__override void board_after_rsmrst(int rsmrst)
{
	/*
	 * b:148688874: If RSMRST# is de-asserted, enable the pull-up on
	 * PG_PP1050_ST_OD.  It won't be enabled prior to this signal going high
	 * because the load switch for PP1050_ST cannot pull the PG low.  Once
	 * it's asserted, disable the pull up so we don't inidicate that the
	 * power is good before the rail is actually ready.
	 */
	int flags = rsmrst ? GPIO_PULL_UP : 0;

	flags |= GPIO_INT_BOTH;

	gpio_set_flags(GPIO_PG_PP1050_ST_OD, flags);
}

uint32_t pp3300_a_pgood;
__override int intel_x86_get_pg_ec_dsw_pwrok(void)
{
	/*
	 * The PP3300_A rail is an input to generate DPWROK.  Assuming that
	 * power is good if voltage is at least 80% of nominal level.  We cannot
	 * read the ADC values during an interrupt, therefore, this power good
	 * value is updated via ADC threshold interrupts.
	 */
	return pp3300_a_pgood;
}

__override int intel_x86_get_pg_ec_all_sys_pwrgd(void)
{
	/*
	 * ALL_SYS_PWRGD is an AND of DRAM PGOOD, VCCST PGOOD, and VCCIO_EXT
	 * PGOOD.
	 */
	return gpio_get_level(GPIO_PG_PP1050_ST_OD) &&
		gpio_get_level(GPIO_PG_DRAM_OD) &&
		gpio_get_level(GPIO_PG_VCCIO_EXT_OD);
}

__override int power_signal_get_level(enum gpio_signal signal)
{
	if (signal == GPIO_PG_EC_DSW_PWROK)
		return intel_x86_get_pg_ec_dsw_pwrok();

	if (signal == GPIO_PG_EC_ALL_SYS_PWRGD)
		return intel_x86_get_pg_ec_all_sys_pwrgd();

	if (IS_ENABLED(CONFIG_HOSTCMD_ESPI)) {
		/* Check signal is from GPIOs or VWs */
		if (espi_signal_is_vw(signal))
			return espi_vw_get_wire(signal);
	}
	return gpio_get_level(signal);

}

void baseboard_chipset_startup(void)
{
	/* Allow keyboard backlight to be enabled */
	gpio_set_level(GPIO_EN_KB_BL, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, baseboard_chipset_startup,
	     HOOK_PRIO_DEFAULT);

void baseboard_chipset_shutdown(void)
{
	/* Turn off the keyboard backlight if it's on. */
	gpio_set_level(GPIO_EN_KB_BL, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, baseboard_chipset_shutdown,
	     HOOK_PRIO_DEFAULT);

void board_hibernate_late(void)
{
	/*
	 * Turn on the Z state.  This will not return as it will cut power to
	 * the EC.
	 */
	gpio_set_level(GPIO_EN_SLP_Z, 1);
}

int board_is_i2c_port_powered(int port)
{
	if (port != I2C_PORT_SENSOR)
		return 1;

	/* Sensor rails are off in S5/G3 */
	return chipset_in_state(CHIPSET_STATE_ANY_OFF) ? 0 : 1;
}
