/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "lid_switch.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>

LOG_MODULE_REGISTER(moonstone_touch, LOG_LEVEL_INF);

/* touch panel power sequence control */

#define TOUCH_ENABLE_DELAY_MS 10

static void touch_disable_deferred(struct k_work *work)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en), 0);
}

static K_WORK_DELAYABLE_DEFINE(touch_disable_deferred_data,
			       touch_disable_deferred);

static void touch_enable_deferred(struct k_work *work)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en), 1);
}

static K_WORK_DELAYABLE_DEFINE(touch_enable_deferred_data,
			       touch_enable_deferred);

/* Called on AP S3 -> S5 transition */
void touch_enable_event_handler(struct ap_power_ev_callback *cb,
				struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_SHUTDOWN:
		/* Cancel touch_enable touch_disable k_work. */
		k_work_cancel_delayable(&touch_enable_deferred_data);
		k_work_cancel_delayable(&touch_disable_deferred_data);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_touch_en), 0);
		break;
	default:
		return;
	}
}

void soc_edp_bl_interrupt(const struct device *device,
			  struct gpio_callback *callback, gpio_port_pins_t pins)
{
	int state;

	state = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_soc_edp_bl_en));

	LOG_INF("%s: %d", __func__, state);

	if (state) {
		k_work_schedule(&touch_enable_deferred_data,
				K_MSEC(TOUCH_ENABLE_DELAY_MS));
	} else {
		k_work_schedule(&touch_disable_deferred_data, K_NO_WAIT);
	}
}

static void touch_lid_change(void)
{
	if (lid_is_open()) {
		k_work_schedule(&touch_enable_deferred_data, K_NO_WAIT);
	} else {
		k_work_schedule(&touch_disable_deferred_data, K_NO_WAIT);
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, touch_lid_change, HOOK_PRIO_DEFAULT);

static void touch_enable_init(void)
{
	static struct ap_power_ev_callback power_cb;
	static struct gpio_callback cb;
	const struct gpio_dt_spec *const toggle_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_soc_edp_bl_en);

	int rv, irq_key;

	ap_power_ev_init_callback(&power_cb, touch_enable_event_handler,
				  AP_POWER_SHUTDOWN | AP_POWER_HARD_OFF);
	ap_power_ev_add_callback(&power_cb);

	gpio_init_callback(&cb, soc_edp_bl_interrupt, BIT(toggle_gpio->pin));
	gpio_add_callback(toggle_gpio->port, &cb);

	rv = gpio_pin_interrupt_configure_dt(toggle_gpio, GPIO_INT_EDGE_BOTH);
	__ASSERT(rv == 0,
		 "touch panel interrupt configuration returned error %d", rv);
	/*
	 * Run the touch_panel handler once to ensure output is in sync.
	 * Lock interrupts to ensure that we don't cause desync if an
	 * interrupt comes in between the internal read of the input
	 * and write to the output.
	 */
	irq_key = irq_lock();
	soc_edp_bl_interrupt(toggle_gpio->port, &cb, BIT(toggle_gpio->pin));
	irq_unlock(irq_key);

	return;
}
DECLARE_HOOK(HOOK_INIT, touch_enable_init, HOOK_PRIO_POST_FIRST);
