/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "ap_power/ap_power.h"
#include "base_state.h"
#include "chipset.h"
#include "console.h"
#include "drivers/one_wire_uart.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "host_command.h"
#include "tablet_mode.h"

#include <zephyr/drivers/gpio.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

/* Base detection and debouncing */
#define BASE_DETECT_EN_DEBOUNCE_US (350 * USEC_PER_MSEC)
#define BASE_DETECT_DIS_DEBOUNCE_US (20 * USEC_PER_MSEC)

/*
 * If the base status is unclear (i.e. not within expected ranges, read
 * the ADC value again every 500ms.
 */
#define BASE_DETECT_RETRY_US (500 * USEC_PER_MSEC)

#define ATTACH_MAX_THRESHOLD_MV 400
#define DETACH_MIN_THRESHOLD_MV 2900

static uint64_t base_detect_debounce_time;
static bool detect_base_enabled;

static void base_detect_deferred(void);
DECLARE_DEFERRED(base_detect_deferred);

enum base_status {
	BASE_UNKNOWN = 0,
	BASE_DISCONNECTED = 1,
	BASE_CONNECTED = 2,
};

static enum base_status current_base_status;

static void base_update(enum base_status attached)
{
	int connected = (attached != BASE_CONNECTED) ? false : true;
	const struct gpio_dt_spec *en_cc_lid_base_pu =
		GPIO_DT_FROM_NODELABEL(en_cc_lid_base_pu);
#ifndef CONFIG_ZTEST
	const static struct device *one_wire_uart =
		DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));
#endif

	if (current_base_status == attached)
		return;

	current_base_status = attached;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(en_pp3300_base_x), connected);
#ifndef CONFIG_ZTEST
	if (connected) {
		one_wire_uart_enable(one_wire_uart);
	} else {
		one_wire_uart_disable(one_wire_uart);
	}
#endif

	base_set_state(connected);
	tablet_set_mode(!connected, TABLET_TRIGGER_BASE);

	gpio_pin_configure(en_cc_lid_base_pu->port, en_cc_lid_base_pu->pin,
			   connected ? GPIO_OUTPUT_HIGH : GPIO_INPUT);
}

void base_detect_interrupt(enum gpio_signal signal)
{
	uint64_t time_now = get_time().val;
	hook_call_deferred(&base_detect_deferred_data,
			   (current_base_status == BASE_CONNECTED) ?
				   BASE_DETECT_DIS_DEBOUNCE_US :
				   BASE_DETECT_EN_DEBOUNCE_US);

	base_detect_debounce_time =
		time_now + ((current_base_status == BASE_CONNECTED) ?
				    BASE_DETECT_DIS_DEBOUNCE_US :
				    BASE_DETECT_EN_DEBOUNCE_US);
}

static void base_detect_deferred(void)
{
	uint64_t time_now = get_time().val;

	if (base_detect_debounce_time > time_now) {
		hook_call_deferred(&base_detect_deferred_data,
				   base_detect_debounce_time - time_now);
		return;
	}
	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(pogo_prsnt_int));
	int mv = adc_read_channel(ADC_BASE_DET);
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(pogo_prsnt_int));

	if (mv >= DETACH_MIN_THRESHOLD_MV) {
		base_update(BASE_DISCONNECTED);
	} else if (mv <= ATTACH_MAX_THRESHOLD_MV) {
		if (current_base_status != BASE_CONNECTED) {
			base_update(BASE_CONNECTED);
		}
	} else {
		hook_call_deferred(&base_detect_deferred_data,
				   BASE_DETECT_RETRY_US);
	}
}

static inline void detect_and_update_base_status(void)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(pogo_prsnt_int_l))) {
		base_update(BASE_CONNECTED);
	} else {
		base_update(BASE_DISCONNECTED);
	}
}

static void base_detect_enable(bool enable)
{
	detect_base_enabled = enable;
	if (enable) {
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(pogo_prsnt_int));
		detect_and_update_base_status();
	} else {
		gpio_disable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(pogo_prsnt_int));
		hook_call_deferred(&base_detect_deferred_data, -1);
	}
}

static void base_startup_hook(struct ap_power_ev_callback *cb,
			      struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_STARTUP:
		base_detect_enable(true);
		break;
	case AP_POWER_SHUTDOWN:
		base_detect_enable(false);
		break;
	default:
		return;
	}
}

static void base_init(void)
{
	static struct ap_power_ev_callback cb;

	detect_base_enabled = false;
	ap_power_ev_init_callback(&cb, base_startup_hook,
				  AP_POWER_STARTUP | AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&cb);

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		base_detect_enable(true);
	}
}
DECLARE_HOOK(HOOK_INIT, base_init, HOOK_PRIO_DEFAULT);

void base_force_state(enum ec_set_base_state_cmd state)
{
	switch (state) {
	case EC_SET_BASE_STATE_ATTACH:
		base_update(BASE_CONNECTED);
		base_detect_enable(false);
		break;
	case EC_SET_BASE_STATE_DETACH:
		base_update(BASE_DISCONNECTED);
		base_detect_enable(false);
		break;
	case EC_SET_BASE_STATE_RESET:
		base_detect_enable(true);
		break;
	}
}
