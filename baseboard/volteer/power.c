/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "hooks.h"
#include "power/icelake.h"
#include "throttle_ap.h"

/*
 * PWROK signal configuration, see the PWROK Generation Flow Diagram (Figure
 * 235) in the Tiger Lake Platform Design Guide for the list of potential
 * signals.
 *
 * Volteer uses this power sequence:
 *	GPIO_EN_PPVAR_VCCIN - Turns on the VCCIN rail. Also used as a delay to
 *		the VCCST_PWRGD input to the AP so this signal must be delayed
 *		5 ms to meet the tCPU00 timing requirement.
 *	GPIO_EC_PCH_SYS_PWROK - Asserts the SYS_PWROK input to the AP. Delayed
 *		a total of 50 ms after ALL_SYS_PWRGD input is asserted. See
 *		b/144478941 for full discussion.
 *
 * Volteer does not provide direct EC control for the VCCST_PWRGD and PCH_PWROK
 * signals. If your board adds these signals to the EC, copy this array
 * to your board.c file and modify as needed.
 */
const struct intel_x86_pwrok_signal pwrok_signal_assert_list[] = {
	{
		.gpio = GPIO_EN_PPVAR_VCCIN,
		.delay_ms = 5,
	},
	{
		.gpio = GPIO_EC_PCH_SYS_PWROK,
		.delay_ms = 50 - 5,
	},
};
const int pwrok_signal_assert_count = ARRAY_SIZE(pwrok_signal_assert_list);

const struct intel_x86_pwrok_signal pwrok_signal_deassert_list[] = {
	/* No delays needed during S0 exit */
	{
		.gpio = GPIO_EC_PCH_SYS_PWROK,
	},
	/* Turn off VCCIN last */
	{
		.gpio = GPIO_EN_PPVAR_VCCIN,
	},
};
const int pwrok_signal_deassert_count = ARRAY_SIZE(pwrok_signal_deassert_list);

static const struct prochot_cfg volteer_prochot_cfg = {
	.gpio_prochot_in = GPIO_EC_PROCHOT_IN_L,
#ifdef CONFIG_CPU_PROCHOT_GATE_ON_C10
	.gpio_c10_in = GPIO_CPU_C10_GATE_L,
	.c10_active_high = false,
#endif
};

static void baseboard_init(void)
{
	/* Enable monitoring of the PROCHOT input to the EC */
	throttle_ap_config_prochot(&volteer_prochot_cfg);
	gpio_enable_interrupt(GPIO_EC_PROCHOT_IN_L);

#ifdef CONFIG_CPU_PROCHOT_GATE_ON_C10
	gpio_enable_interrupt(GPIO_CPU_C10_GATE_L);
#endif
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_DEFAULT);
