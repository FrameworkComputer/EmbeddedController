/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "base_state.h"
#include "chipset.h"
#include "console.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "host_command.h"
#include "tablet_mode.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

#define BASE_DETECT_RETRY_US (500 * MSEC)
/* Base detection debouncing */
#define BASE_DETECT_EN_DEBOUNCE_US (350 * MSEC)
#define BASE_DETECT_DIS_DEBOUNCE_US (20 * MSEC)

K_MUTEX_DEFINE(modify_base_detection_mutex);
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

static void base_update(enum base_status specified_status)
{
	int connected = (specified_status != BASE_CONNECTED) ? false : true;
	uint64_t time_now = get_time().val;

	if (base_detect_debounce_time > time_now)
		hook_call_deferred(&base_detect_deferred_data,
				   base_detect_debounce_time - time_now);

	if (current_base_status == specified_status)
		return;

	current_base_status = specified_status;

	base_set_state(connected);
	tablet_set_mode(!connected, TABLET_TRIGGER_BASE);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(en_pp3300_base_x), connected);
}

void base_detect_interrupt(enum gpio_signal signal)
{
	uint64_t time_now = get_time().val;

	gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(pogo_prsnt_int));
	hook_call_deferred(&base_detect_deferred_data,
			   (current_base_status == BASE_CONNECTED) ?
				   BASE_DETECT_DIS_DEBOUNCE_US :
				   BASE_DETECT_EN_DEBOUNCE_US);

	base_detect_debounce_time = time_now + BASE_DETECT_RETRY_US;
}

static inline void detect_and_update_base_status(void)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(pogo_prsnt_int_l))) {
		base_update(BASE_CONNECTED);
	} else {
		base_update(BASE_DISCONNECTED);
	}
}

static void base_detect_deferred(void)
{
	k_mutex_lock(&modify_base_detection_mutex, K_FOREVER);
	/*
	 * If a disable base detection is issued after an ISR, and is before
	 * executing the deferred hook, then we need to check whether the
	 * detection is enabled. If disabled, there is no need to re-enable the
	 * interrupt.
	 */
	if (detect_base_enabled) {
		detect_and_update_base_status();
		gpio_enable_dt_interrupt(
			GPIO_INT_FROM_NODELABEL(pogo_prsnt_int));
	}
	k_mutex_unlock(&modify_base_detection_mutex);
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
		base_update(BASE_UNKNOWN);
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
	gpio_enable_dt_interrupt(
		GPIO_INT_FROM_NODELABEL(int_lid_open_for_pogo));
}
DECLARE_HOOK(HOOK_INIT, base_init, HOOK_PRIO_DEFAULT);

void base_force_state(enum ec_set_base_state_cmd state)
{
	k_mutex_lock(&modify_base_detection_mutex, K_FOREVER);
	switch (state) {
	case EC_SET_BASE_STATE_ATTACH:
		base_detect_enable(false);
		base_update(BASE_CONNECTED);
		break;
	case EC_SET_BASE_STATE_DETACH:
		base_detect_enable(false);
		base_update(BASE_DISCONNECTED);
		break;
	case EC_SET_BASE_STATE_RESET:
		base_detect_enable(true);
		break;
	}
	k_mutex_unlock(&modify_base_detection_mutex);
}

void enable_base_by_lid(void)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(lid_open))) {
		base_detect_enable(true);
	} else {
		base_detect_enable(false);
	}
}
