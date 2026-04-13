/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "ap_power/ap_power.h"
#include "base_state.h"
#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "extpower.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "host_command.h"
#include "tablet_mode.h"
#include "util.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

K_MUTEX_DEFINE(modify_base_detection_mutex);

#define BASE_DETECT_INTERVAL (30 * USEC_PER_MSEC)
#define BASE_DETECT_EN_DEBOUNCE_US (300 * USEC_PER_MSEC)
#define BASE_DETECT_DIS_DEBOUNCE_US (0 * USEC_PER_MSEC)

#define BASE_ATTACH_TH_NORMAL_MV 2500
#define BASE_ATTACH_TH_LOW_MV 1000
#define BASE_SOC_THRESHOLD 10

static bool attached;
static bool debouncing;

static int base_get_threshold(void)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_pogo_low_pwr_sw)))
		return BASE_ATTACH_TH_LOW_MV;

	return BASE_ATTACH_TH_NORMAL_MV;
}

static void base_update(void);
DECLARE_DEFERRED(base_update);

static void base_batt_soc_setting(void);
DECLARE_DEFERRED(base_batt_soc_setting);

static void base_update(void)
{
	base_set_state(attached);
	tablet_set_mode(!attached, TABLET_TRIGGER_BASE);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ppvar_base_x), attached);

	hook_call_deferred(&base_batt_soc_setting_data, 0);
}

static void base_detect_tick(void);
DECLARE_DEFERRED(base_detect_tick);

static void base_detect_tick(void)
{
	int next_us = BASE_DETECT_INTERVAL;
	int mv = adc_read_channel(ADC_BASE_DET);
	int threshold = base_get_threshold();

	if ((mv > threshold) && base_get_state()) {
		if (!debouncing) {
			debouncing = true;
			next_us = BASE_DETECT_DIS_DEBOUNCE_US;
		} else {
			debouncing = false;
			attached = false;
			CPRINTS("Base detached (adc=%d mV)", mv);
			base_update();
		}
	} else if (mv <= threshold && !base_get_state()) {
		if (!debouncing) {
			debouncing = true;
			next_us = BASE_DETECT_EN_DEBOUNCE_US;
		} else {
			debouncing = false;
			attached = true;
			CPRINTS("Base attached (adc=%d mV)", mv);
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
		if (attached)
			hook_call_deferred(&base_update_data, 0);
		break;
	case AP_POWER_SHUTDOWN:
		if (!extpower_is_present())
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

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF) || extpower_is_present()) {
		base_detect_enable(true);
	}

	return 0;
}

SYS_INIT(base_init, APPLICATION, 1);

void base_init_setting(void)
{
	if (adc_read_channel(ADC_BASE_DET) > base_get_threshold()) {
		attached = false;
		hook_call_deferred(&base_update_data, 0);
	} else {
		attached = true;
		hook_call_deferred(&base_update_data, 0);
	}
	base_detect_enable(true);
}
DECLARE_HOOK(HOOK_INIT, base_init_setting, HOOK_PRIO_DEFAULT);

static void base_batt_soc_setting(void)
{
	int curr_batt_lvl = DIV_ROUND_NEAREST(charge_get_display_charge(), 10);
	bool ext_power = extpower_is_present();
	int current_gpio_level = gpio_pin_get_dt(
		GPIO_DT_FROM_NODELABEL(gpio_ec_pogo_low_pwr_sw));

	if (!attached || debouncing) {
		if (current_gpio_level)
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(gpio_ec_pogo_low_pwr_sw),
				false);
		return;
	}

	int target_level =
		(curr_batt_lvl < BASE_SOC_THRESHOLD && !ext_power) ? 1 : 0;

	if (current_gpio_level != target_level) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_pogo_low_pwr_sw),
				target_level);
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, base_batt_soc_setting, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, base_batt_soc_setting, HOOK_PRIO_DEFAULT);

static void base_setting_on_shutdown(void)
{
	if (extpower_is_present() && chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		base_detect_enable(true);
	} else if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		base_detect_enable(false);
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, base_setting_on_shutdown, HOOK_PRIO_PRE_DEFAULT);

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
