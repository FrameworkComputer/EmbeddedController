/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#include "driver/cec/bitbang.h"
#include "ec_tasks.h"
#include "gpio/gpio_int.h"
#include "task.h"
#include "timer.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/cec_counter.h>

LOG_MODULE_REGISTER(cec_counter, LOG_LEVEL_ERR);

BUILD_ASSERT(DT_HAS_CHOSEN(cros_ec_cec_counter),
	     "a cros-ec,cec-counter device must be chosen");
#define cec_counter_dev DEVICE_DT_GET(DT_CHOSEN(cros_ec_cec_counter))

/* Timestamp when the most recent interrupt occurred */
static timestamp_t interrupt_time;

/* Timestamp when the second most recent interrupt occurred */
static timestamp_t prev_interrupt_time;

/* Flag set when a transfer is initiated from the AP */
static bool transfer_initiated;

/* The capture edge we're waiting for */
static enum cec_cap_edge expected_cap_edge;

__override void cec_update_interrupt_time(int port)
{
	prev_interrupt_time = interrupt_time;
	interrupt_time = get_time();
}

void cec_ext_timer_interrupt(int port)
{
	if (transfer_initiated) {
		transfer_initiated = false;
		cec_event_tx(port);
	} else {
		cec_update_interrupt_time(port);
		cec_event_timeout(port);
	}
}

void cec_ext_top_timer_handler(const struct device *dev, void *user_data)
{
	cec_ext_timer_interrupt((int)((intptr_t)user_data));
}

void cec_gpio_interrupt(enum gpio_signal signal)
{
	int port;
	int level;
	const struct bitbang_cec_config *drv_config;

	for (port = 0; port < CEC_PORT_COUNT; port++) {
		if (cec_config[port].drv == &bitbang_cec_drv) {
			drv_config = cec_config[port].drv_config;
			if (drv_config->gpio_in == signal)
				break;
		}
	}

	/* Invalid port, return here. */
	if (port < 0 || port >= CEC_PORT_COUNT) {
		LOG_ERR("Invalid CEC port for %s", __func__);
		return;
	}

	cec_update_interrupt_time(port);

	level = gpio_pin_get_dt(gpio_get_dt_spec(signal));
	if (!((expected_cap_edge == CEC_CAP_EDGE_FALLING && level == 0) ||
	      (expected_cap_edge == CEC_CAP_EDGE_RISING && level == 1)))
		return;

	cec_event_cap(port);
}

void cros_cec_bitbang_tmr_cap_start(int port, enum cec_cap_edge edge,
				    int timeout)
{
	expected_cap_edge = edge;

	if (timeout > 0) {
		/*
		 * Take into account the delay from when the interrupt occurs to
		 * when we actually get here. Since the timing is done in
		 * software, there is an additional unknown delay from when the
		 * interrupt occurs to when the ISR starts. Empirically, this
		 * seems to be about 100 us, so account for this too.
		 */
		int delay = (get_time().val - interrupt_time.val + 100);
		int timer_count =
			counter_us_to_ticks(cec_counter_dev, (timeout - delay));
		struct counter_top_cfg top_cfg;

		/*
		 * Handle the case where the delay is greater than the timeout.
		 * This should never actually happen for typical delay and
		 * timeout values.
		 */
		if (timer_count < 0) {
			timer_count = 0;
			LOG_WRN("CEC%d warning: timer_count < 0", port);
		}

		/*
		 * Start the timer and enable the timer interrupt
		 */
		top_cfg.ticks = timer_count;
		top_cfg.callback = cec_ext_top_timer_handler;
		top_cfg.user_data = (void *)((intptr_t)port);
		top_cfg.flags = 0;
		counter_set_top_value(cec_counter_dev, &top_cfg);
	} else {
		counter_stop(cec_counter_dev);
	}
}

void cros_cec_bitbang_tmr_cap_stop(int port)
{
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;

	gpio_pin_interrupt_configure_dt(gpio_get_dt_spec(drv_config->gpio_in),
					GPIO_INT_DISABLE);

	counter_stop(cec_counter_dev);
}

int cros_cec_bitbang_tmr_cap_get(int port)
{
	return counter_us_to_ticks(cec_counter_dev, (interrupt_time.val -
						     prev_interrupt_time.val));
}

void cros_cec_bitbang_debounce_enable(int port)
{
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;

	gpio_pin_interrupt_configure_dt(gpio_get_dt_spec(drv_config->gpio_in),
					GPIO_INT_DISABLE);
}

void cros_cec_bitbang_debounce_disable(int port)
{
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;

	gpio_pin_interrupt_configure_dt(gpio_get_dt_spec(drv_config->gpio_in),
					GPIO_INT_ENABLE | GPIO_INT_EDGE_BOTH);
}

void cros_cec_bitbang_trigger_send(int port)
{
	unsigned int key;
	/* Elevate to interrupt context */
	transfer_initiated = true;

	key = irq_lock();
	cec_ext_timer_interrupt(port);
	irq_unlock(key);
}

void cros_cec_bitbang_enable_timer(int port)
{
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;

	/*
	 * Enable gpio interrupts. Timer interrupts will be enabled as needed by
	 * cec_tmr_cap_start().
	 */
	gpio_pin_interrupt_configure_dt(gpio_get_dt_spec(drv_config->gpio_in),
					GPIO_INT_ENABLE | GPIO_INT_EDGE_BOTH);
}

void cros_cec_bitbang_disable_timer(int port)
{
	cec_tmr_cap_stop(port);

	interrupt_time.val = 0;
	prev_interrupt_time.val = 0;
}
