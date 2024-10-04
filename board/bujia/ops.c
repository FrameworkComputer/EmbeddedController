/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "power.h"
#include "power_button.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/* Debounce time for PS_ON# signal */
#define PS_ON_DEBOUNCE_MS 50

#define LONG_PRESS_MS 3000
#define SHORT_PRESS_MS 200
#define RESUME_DELAY_MS 1000

static void ps_on_translate_to_shutdown_deferred(void)
{
	if (chipset_in_state(CHIPSET_STATE_ON))
		/* long press power button to power off the device */
		power_button_simulate_press(LONG_PRESS_MS);
}
DECLARE_DEFERRED(ps_on_translate_to_shutdown_deferred);

static void ps_on_irq_deferred(void)
{
	int ps_on;

	ps_on = gpio_get_level(GPIO_OPS_PS_ON);

	/*
	 * PS_ON# is asserted. If device is off, power on the device.
	 * If device is in suspend, wake up then power off the device.
	 * If device is on and not in suspend, power off the device.
	 */
	if (!ps_on) {
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			/* short press power button to power on device */
			power_button_simulate_press(SHORT_PRESS_MS);
		} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
			/*
			 * short press power button to wake up device from
			 * suspend, wait for 1 second to make sure the device is
			 * resumed, long press power button to power off the
			 * device.
			 */
			power_button_simulate_press(SHORT_PRESS_MS);
			hook_call_deferred(
				&ps_on_translate_to_shutdown_deferred_data,
				RESUME_DELAY_MS * MSEC);
		} else {
			/* long press power button to power off the device */
			power_button_simulate_press(LONG_PRESS_MS);
		}
	}
}
DECLARE_DEFERRED(ps_on_irq_deferred);

void ps_on_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&ps_on_irq_deferred_data, PS_ON_DEBOUNCE_MS * MSEC);
}
