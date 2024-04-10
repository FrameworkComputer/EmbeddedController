/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor/fpsensor_detect.h"
#include "hooks.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>

static struct k_work slp_event_work;
static struct gpio_callback slp_event_callback;
static struct gpio_callback slp_alt_event_callback;

static void slp_event_handler(struct k_work *work)
{
	static bool suspend_allowed = true;
	/*
	 * Some platforms have a broken SLP_S0_L signal (stuck to 0 in S0)
	 * if set, ignore it and only uses SLP_S3_L for the AP state.
	 */
	bool broken_slp;

	/*
	 * The Zork variants currently have a broken SLP_S0_L signal
	 * (stuck to 0 in S0). For now, unconditionally ignore it here
	 * as they are the only UART users and the AP has no S0ix state.
	 * TODO(b/174695987) once the RW AP firmware has been updated
	 * on all those machines, remove this workaround.
	 */
	broken_slp = get_fp_transport_type() == FP_TRANSPORT_TYPE_UART;

	int running =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(slp_alt_l)) &&
		(gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(slp_l)) || broken_slp);

	if (running) { /* S0 */
		if (suspend_allowed) {
			pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_IDLE,
						 PM_ALL_SUBSTATES);
			suspend_allowed = false;
		}
		/* Added for compatibility with CrosEC, even if there are no
		 * consumers for this signal.
		 */
		hook_notify(HOOK_CHIPSET_RESUME);
	} else { /* S0ix/S3 */
		hook_notify(HOOK_CHIPSET_SUSPEND);
		if (!suspend_allowed) {
			pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_IDLE,
						 PM_ALL_SUBSTATES);
			suspend_allowed = true;
		}
	}
}

static void slp_event_isr(const struct device *port, struct gpio_callback *cb,
			  gpio_port_pins_t pins)
{
	/* Use k_work to call hook_notify */
	k_work_submit(&slp_event_work);
}

static int slp_event_init(void)
{
	k_work_init(&slp_event_work, slp_event_handler);

	/* Enable and configure interrupts for both sleep pins */
	gpio_init_callback(&slp_event_callback, slp_event_isr,
			   BIT(GPIO_DT_FROM_NODELABEL(slp_l)->pin));
	gpio_add_callback_dt(GPIO_DT_FROM_NODELABEL(slp_l),
			     &slp_event_callback);
	gpio_pin_interrupt_configure_dt(GPIO_DT_FROM_NODELABEL(slp_l),
					GPIO_INT_EDGE_BOTH);

	gpio_init_callback(&slp_alt_event_callback, slp_event_isr,
			   BIT(GPIO_DT_FROM_NODELABEL(slp_alt_l)->pin));
	gpio_add_callback_dt(GPIO_DT_FROM_NODELABEL(slp_alt_l),
			     &slp_alt_event_callback);
	gpio_pin_interrupt_configure_dt(GPIO_DT_FROM_NODELABEL(slp_alt_l),
					GPIO_INT_EDGE_BOTH);

	/* Get init state of the sleep pins */
	k_work_submit(&slp_event_work);

	return 0;
}
SYS_INIT(slp_event_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

static int gpio_init(void)
{
	const struct device *dev_gpioc = DEVICE_DT_GET(DT_NODELABEL(gpioc));
	const struct device *dev_gpioh = DEVICE_DT_GET(DT_NODELABEL(gpioh));

	pm_device_action_run(dev_gpioc, PM_DEVICE_ACTION_SUSPEND);
	pm_device_action_run(dev_gpioh, PM_DEVICE_ACTION_SUSPEND);

	return 0;
}
SYS_INIT(gpio_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
