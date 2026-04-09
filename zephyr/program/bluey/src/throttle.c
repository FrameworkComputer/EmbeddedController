/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "throttle_ap.h"

#include <zephyr/devicetree.h>

static const struct prochot_cfg prochot_cfg = {
	.gpio_prochot_in = GPIO_CPU_PROCHOT,
};

void chipset_throttle_cpu(int throttle)
{
	if (!chipset_in_state(CHIPSET_STATE_ON))
		return;

	if (throttle) {
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_prochot));
		/* Drive PROCHOT to active level */
		gpio_set_level(
			GPIO_CPU_PROCHOT,
			!IS_ENABLED(
				CONFIG_PLATFORM_EC_POWERSEQ_CPU_PROCHOT_ACTIVE_LOW));
		gpio_set_flags(GPIO_CPU_PROCHOT, GPIO_OUTPUT | GPIO_OPEN_DRAIN);
	} else {
		/* Return to input mode to allow CPU to drive the signal */
		gpio_set_flags(GPIO_CPU_PROCHOT, GPIO_INPUT | GPIO_PULL_UP);
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_prochot));
	}
}

static void init_throttle(void)
{
	/* Enable prochot interrupt */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_prochot));
	throttle_ap_config_prochot(&prochot_cfg);
}
DECLARE_HOOK(HOOK_INIT, init_throttle, HOOK_PRIO_DEFAULT);
