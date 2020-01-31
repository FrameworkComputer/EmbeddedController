/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Dedede family-specific configuration */

#include "adc.h"
#include "chipset.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "intel_x86.h"

/*
 * Dedede does not use hibernate wake pins, but the super low power "Z-state"
 * instead in which the EC is powered off entirely.  Power will be restored to
 * the EC once one of the wake up events occurs.  These events are ACOK, lid
 * open, and a power button press.
 */
const enum gpio_signal hibernate_wake_pins[] = {};
const int hibernate_wake_pins_used;

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
	 * ALL_SYS_PWRGD is an AND of both DRAM PGOOD and VCCST PGOOD.
	 */
	return gpio_get_level(GPIO_PG_PP1050_ST_OD) &&
		gpio_get_level(GPIO_PG_DRAM_OD);
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
