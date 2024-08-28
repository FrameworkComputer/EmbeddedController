/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
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

K_MUTEX_DEFINE(modify_base_detection_mutex);

#define BASE_DETECT_INTERVAL (30 * MSEC)
#define BASE_DETECT_EN_DEBOUNCE_US (300 * MSEC)
#define BASE_DETECT_DIS_DEBOUNCE_US (0 * MSEC)

#define ATTACH_MAX_THRESHOLD_MV 500
#define ATTACH_MIN_THRESHOLD_MV 40

static bool attached;

static void base_update(void);
DECLARE_DEFERRED(base_update);

static void base_update(void)
{
	base_set_state(attached);
	tablet_set_mode(!attached, TABLET_TRIGGER_BASE);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(en_pp5000_base_x), attached);
}

static void base_detect_tick(void);
DECLARE_DEFERRED(base_detect_tick);

static void base_detect_tick(void)
{
	static bool debouncing;
	int next_us = BASE_DETECT_INTERVAL;

	int mv = adc_read_channel(ADC_BASE_DET);
	if ((mv > ATTACH_MAX_THRESHOLD_MV || mv < ATTACH_MIN_THRESHOLD_MV) &&
	    base_get_state()) {
		if (!debouncing) {
			debouncing = true;
			next_us = BASE_DETECT_DIS_DEBOUNCE_US;
		} else {
			debouncing = false;
			attached = false;
			base_update();
		}
	} else if (mv >= ATTACH_MIN_THRESHOLD_MV &&
		   mv <= ATTACH_MAX_THRESHOLD_MV && !base_get_state()) {
		if (!debouncing) {
			debouncing = true;
			next_us = BASE_DETECT_EN_DEBOUNCE_US;
		} else {
			debouncing = false;
			attached = true;
			base_update();
		}
	} else {
		debouncing = false;
	}
	hook_call_deferred(&base_detect_tick_data, next_us);
}

static void base_detect_enable(bool enable)
{
	if (enable) {
		hook_call_deferred(&base_detect_tick_data,
				   BASE_DETECT_INTERVAL);
	} else {
		hook_call_deferred(&base_detect_tick_data, -1);
		attached = false;
		hook_call_deferred(&base_update_data, 0);
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

static int base_init(void)
{
	static struct ap_power_ev_callback cb;

	ap_power_ev_init_callback(&cb, base_startup_hook,
				  AP_POWER_STARTUP | AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&cb);

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		base_detect_enable(true);
	}

	return 0;
}

SYS_INIT(base_init, APPLICATION, 1);

void base_init_setting(void)
{
	if (adc_read_channel(ADC_BASE_DET) > ATTACH_MAX_THRESHOLD_MV ||
	    adc_read_channel(ADC_BASE_DET) < ATTACH_MIN_THRESHOLD_MV) {
		attached = false;
		hook_call_deferred(&base_update_data, 0);
	} else {
		attached = true;
		hook_call_deferred(&base_update_data, 0);
	}
	base_detect_enable(true);
}
DECLARE_HOOK(HOOK_INIT, base_init_setting, HOOK_PRIO_DEFAULT);

void base_force_state(enum ec_set_base_state_cmd state)
{
	k_mutex_lock(&modify_base_detection_mutex, K_FOREVER);
	switch (state) {
	case EC_SET_BASE_STATE_ATTACH:
		base_detect_enable(false);
		attached = true;
		base_update();
		break;
	case EC_SET_BASE_STATE_DETACH:
		base_detect_enable(false);
		attached = false;
		base_update();
		break;
	case EC_SET_BASE_STATE_RESET:
		base_detect_enable(true);
		break;
	}
	k_mutex_unlock(&modify_base_detection_mutex);
}
