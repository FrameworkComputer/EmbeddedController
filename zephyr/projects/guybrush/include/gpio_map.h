/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_GPIO_MAP_H
#define __ZEPHYR_GPIO_MAP_H

#include <devicetree.h>
#include <gpio_signal.h>

/* Power input signals */
enum power_signal {
	X86_SLP_S0_N, /* SOC  -> SLP_S3_S0I3_L */
	X86_SLP_S3_N, /* SOC  -> SLP_S3_L */
	X86_SLP_S5_N, /* SOC  -> SLP_S5_L */

	X86_S0_PGOOD, /* PMIC -> S0_PWROK_OD */
	X86_S5_PGOOD, /* PMIC -> S5_PWROK */

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT,
};

/*
 * Set EC_CROS_GPIO_INTERRUPTS to a space-separated list of GPIO_INT items.
 *
 * Each GPIO_INT requires three parameters:
 *   gpio_signal - The enum gpio_signal for the interrupt gpio
 *   interrupt_flags - The interrupt-related flags (e.g. GPIO_INT_EDGE_BOTH)
 *   handler - The platform/ec interrupt handler.
 *
 * Ensure that this files includes all necessary headers to declare all
 * referenced handler functions.
 *
 * For example, one could use the follow definition:
 * #define EC_CROS_GPIO_INTERRUPTS \
 *   GPIO_INT(NAMED_GPIO(h1_ec_pwr_btn_odl), GPIO_INT_EDGE_BOTH, button_print)
 */
#define EC_CROS_GPIO_INTERRUPTS                                              \
	GPIO_INT(GPIO_LID_OPEN, GPIO_INT_EDGE_BOTH, lid_interrupt)           \
	GPIO_INT(GPIO_AC_PRESENT, GPIO_INT_EDGE_BOTH, extpower_interrupt)    \
	GPIO_INT(GPIO_POWER_BUTTON_L, GPIO_INT_EDGE_BOTH,                    \
		 power_button_interrupt)                                     \
	GPIO_INT(GPIO_EC_PWR_BTN_ODL, GPIO_INT_EDGE_BOTH,                    \
		 power_button_interrupt)                                     \
	GPIO_INT(GPIO_PCH_SLP_S3_L, GPIO_INT_EDGE_BOTH, baseboard_en_pwr_s0) \
	GPIO_INT(GPIO_PCH_SLP_S5_L, GPIO_INT_EDGE_BOTH,                      \
		 power_signal_interrupt)                                     \
	GPIO_INT(GPIO_PCH_SLP_S0_L, GPIO_INT_EDGE_BOTH,                      \
		 power_signal_interrupt)                                     \
	GPIO_INT(GPIO_S5_PGOOD, GPIO_INT_EDGE_BOTH, extpower_interrupt)      \
	GPIO_INT(GPIO_S0_PGOOD, GPIO_INT_EDGE_BOTH, power_signal_interrupt)  \
	GPIO_INT(GPIO_EC_PCORE_INT_ODL, GPIO_INT_EDGE_BOTH,                  \
		 power_signal_interrupt)                                     \
	GPIO_INT(GPIO_PG_GROUPC_S0_OD, GPIO_INT_EDGE_BOTH,                   \
		 baseboard_en_pwr_pcore_s0)                                  \
	GPIO_INT(GPIO_PG_LPDDR4X_S3_OD, GPIO_INT_EDGE_BOTH,                  \
		 baseboard_en_pwr_pcore_s0)

#endif /* __ZEPHYR_GPIO_MAP_H */
