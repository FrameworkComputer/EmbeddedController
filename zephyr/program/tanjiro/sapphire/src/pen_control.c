/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "peripheral_charger.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>

#include <ap_power/ap_power.h>

enum pen_status {
	PEN_REMOVED = 0,
	PEN_PRESENT = 1,
};

static enum pen_status pen_status_flag = PEN_REMOVED;

static inline void update_pen_status(void)
{
	pen_status_flag =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pen_pres)) ?
			PEN_REMOVED :
			PEN_PRESENT;
}

static void set_wlc_power(void)
{
	/* Enable or disable WLC power based on pen presence */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pp5000_wlc_en),
			pen_status_flag);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_pp1800_wlc_en),
			pen_status_flag);
}
DECLARE_DEFERRED(set_wlc_power);

void pen_pres_irq(enum gpio_signal signal)
{
	update_pen_status();

	/* Only operate when AP is running (not OFF) */
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		if (pen_status_flag == PEN_PRESENT) {
			hook_call_deferred(&set_wlc_power_data, 0);
		} else {
			hook_call_deferred(&set_wlc_power_data,
					   500 * USEC_PER_MSEC);
		}
	}
}

__override void board_pchg_power_on(int port, bool on)
{
	if (port != 0)
		return;

	if (!on) {
		pen_status_flag = PEN_REMOVED;
	} else {
		update_pen_status();
	}

	hook_call_deferred(&set_wlc_power_data, 0);
}

static void pen_status_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pen_pres));
}
DECLARE_HOOK(HOOK_INIT, pen_status_init, HOOK_PRIO_DEFAULT);
