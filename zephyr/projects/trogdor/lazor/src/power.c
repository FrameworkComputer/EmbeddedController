/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <init.h>
#include <drivers/gpio.h>

#include <ap_power/ap_power.h>
#include "power.h"
#include "task.h"
#include "gpio.h"

static void board_power_change(struct ap_power_ev_callback *cb,
			       struct ap_power_ev_data data)
{
	switch (data.event) {
	default:
		return;

	case AP_POWER_PRE_INIT:
		/* Turn on the 3.3V rail */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp3300_a), 1);

		/* Turn on the 5V rail. */
#ifdef CONFIG_POWER_PP5000_CONTROL
		power_5v_enable(task_get_current(), 1);
#else /* !defined(CONFIG_POWER_PP5000_CONTROL) */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_a), 1);
#endif /* defined(CONFIG_POWER_PP5000_CONTROL) */
		break;

	case AP_POWER_SHUTDOWN_COMPLETE:
		/* Turn off the 5V rail. */
#ifdef CONFIG_POWER_PP5000_CONTROL
		power_5v_enable(task_get_current(), 0);
#else /* !defined(CONFIG_POWER_PP5000_CONTROL) */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_a), 0);
#endif /* defined(CONFIG_POWER_PP5000_CONTROL) */

		/* Turn off the 3.3V and 5V rails. */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp3300_a), 0);
		break;
	}
}

static int board_power_handler_init(const struct device *unused)
{
	static struct ap_power_ev_callback cb;

	/* Setup a suspend/resume callback */
	ap_power_ev_init_callback(&cb, board_power_change,
				  AP_POWER_PRE_INIT |
				  AP_POWER_SHUTDOWN_COMPLETE);
	ap_power_ev_add_callback(&cb);
	return 0;
}
SYS_INIT(board_power_handler_init, APPLICATION, 1);
