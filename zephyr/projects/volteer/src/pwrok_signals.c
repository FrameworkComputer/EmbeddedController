/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWROK signals lists.  Copied from volteer baseboard in platform/ec. */

#include <stdbool.h>
#include <zephyr.h>

#include "gpio_map.h"
#include "gpio_signal.h"
#include "power/intel_x86.h"

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
