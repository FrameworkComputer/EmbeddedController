/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cros_cbi.h"
#include "gpio/gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "lid_switch.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <ap_power/ap_power.h>

LOG_MODULE_REGISTER(board_touch, LOG_LEVEL_INF);

/* touch panel power sequence control */

#define TOUCH_ENABLE_DELAY_MS 500
#define TOUCH_DISABLE_DELAY_MS 0

static bool touch_sequence_enable;

static void touch_disable_deferred(struct k_work *work)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tchscr_report_en), 0);
}

static K_WORK_DELAYABLE_DEFINE(touch_disable_deferred_data,
			       touch_disable_deferred);

static void touch_enable_deferred(struct k_work *work)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_tchscr_report_en), 1);
}

static K_WORK_DELAYABLE_DEFINE(touch_enable_deferred_data,
			       touch_enable_deferred);

/* Called on AP S3 -> S5 transition */
void board_power_event_handler(struct ap_power_ev_callback *cb,
			       struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_SHUTDOWN:
		/* Cancel touch_enable touch_disable k_work. */
		k_work_cancel_delayable(&touch_enable_deferred_data);
		k_work_cancel_delayable(&touch_disable_deferred_data);
		gpio_pin_set_dt(
			GPIO_DT_FROM_NODELABEL(gpio_ec_tchscr_report_en), 0);
		break;
	default:
		return;
	}
}

void soc_edp_bl_interrupt(const struct device *device,
			  struct gpio_callback *callback, gpio_port_pins_t pins)
{
	int state;

	state = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_edp_bl_en_3v3));

	LOG_INF("%s: %d", __func__, state);

	if (state) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od), 1);
		if (touch_sequence_enable) {
			k_work_schedule(&touch_enable_deferred_data,
					K_MSEC(TOUCH_ENABLE_DELAY_MS));
		}
	} else {
		if (touch_sequence_enable) {
			k_work_schedule(&touch_disable_deferred_data,
					K_MSEC(TOUCH_DISABLE_DELAY_MS));
		}
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od), 0);
	}
}

static void touch_lid_change(void)
{
	if (!lid_is_open()) {
		if (touch_sequence_enable) {
			LOG_INF("%s: disable touch", __func__);
			k_work_schedule(&touch_disable_deferred_data,
					K_MSEC(TOUCH_DISABLE_DELAY_MS));
		}
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od), 0);
	} else {
		if (gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_edp_bl_en_3v3))) {
			if (touch_sequence_enable &&
			    !gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(
				    gpio_ec_tchscr_report_en))) {
				LOG_INF("%s: enable touch", __func__);
				k_work_schedule(&touch_enable_deferred_data,
						K_MSEC(TOUCH_ENABLE_DELAY_MS));
			}
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(gpio_ec_bl_en_od), 1);
		}
	}
}
DECLARE_HOOK(HOOK_LID_CHANGE, touch_lid_change, HOOK_PRIO_DEFAULT);

static void touch_enable_init(void)
{
	static struct ap_power_ev_callback power_cb;
	static struct gpio_callback cb;
	const struct gpio_dt_spec *const tpgpio_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_edp_bl_en_3v3);

	int rv, irq_key;
	uint32_t val;

	touch_sequence_enable = false;

	rv = cros_cbi_get_fw_config(FW_PANEL_PWRSEQ_EC_CONTROL, &val);
	if (rv != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d",
			FW_PANEL_PWRSEQ_EC_CONTROL);
		return;
	}

	LOG_INF("%s: %sable", __func__,
		(val == FW_PANEL_PWRSEQ_EC_CONTROL_ENABLE) ? "en" : "dis");

	gpio_init_callback(&cb, soc_edp_bl_interrupt, BIT(tpgpio_gpio->pin));
	gpio_add_callback(tpgpio_gpio->port, &cb);

	rv = gpio_pin_interrupt_configure_dt(tpgpio_gpio, GPIO_INT_EDGE_BOTH);
	__ASSERT(rv == 0,
		 "touch panel interrupt configuration returned error %d", rv);
	/*
	 * Run the touch_panel handler once to ensure output is in sync.
	 * Lock interrupts to ensure that we don't cause desync if an
	 * interrupt comes in between the internal read of the input
	 * and write to the output.
	 */
	irq_key = irq_lock();
	soc_edp_bl_interrupt(tpgpio_gpio->port, &cb, BIT(tpgpio_gpio->pin));
	irq_unlock(irq_key);

	/*
	 * For gpio_bl_en_od also need to follow gpio_edp_bl_en_3v3, the
	 * interrupt of edp_bl_en_3v3 should be enable before the return
	 * caused by unsupported touch enable sequence.
	 */
	if (val != FW_PANEL_PWRSEQ_EC_CONTROL_ENABLE) {
		return;
	}

	ap_power_ev_init_callback(&power_cb, board_power_event_handler,
				  AP_POWER_SHUTDOWN | AP_POWER_HARD_OFF);
	ap_power_ev_add_callback(&power_cb);

	touch_sequence_enable = true;

	return;
}
DECLARE_HOOK(HOOK_INIT, touch_enable_init, HOOK_PRIO_POST_FIRST);
