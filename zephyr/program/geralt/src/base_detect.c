/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "ap_power/ap_power.h"
#include "base_state.h"
#include "chipset.h"
#include "console.h"
#include "hooks.h"
#include "lid_switch.h"
#include "tablet_mode.h"

#include <zephyr/drivers/gpio.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

#define BASE_DETECT_INTERVAL (200 * MSEC)
#define ATTACH_MAX_THRESHOLD_MV 300
#define DETACH_MIN_THRESHOLD_MV 3000

enum power_status {
	POWER_STATUS_NOCHANGE = 0,
	POWER_STATUS_STARTUP = 1,
	POWER_STATUS_SHUTDOWN = 2,
};
static enum power_status power_status_change = POWER_STATUS_NOCHANGE;

static void base_update(bool attached)
{
	const struct gpio_dt_spec *en_cc_lid_base_pu =
		GPIO_DT_FROM_NODELABEL(en_cc_lid_base_pu);

	if (IS_ENABLED(CONFIG_GERALT_LID_DETECTION_SELECTED)) {
		enable_lid_detect(attached);
		if (power_status_change == POWER_STATUS_SHUTDOWN ||
		    chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(en_ppvar_base_x),
					false);
		} else {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(en_ppvar_base_x),
					attached);
		}
		power_status_change = POWER_STATUS_NOCHANGE;
	} else {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(en_ppvar_base_x),
				attached);
	}

	base_set_state(attached);
	tablet_set_mode(!attached, TABLET_TRIGGER_BASE);

	gpio_pin_configure(en_cc_lid_base_pu->port, en_cc_lid_base_pu->pin,
			   attached ? GPIO_OUTPUT_HIGH : GPIO_INPUT);
}

static void base_detect_tick(void);
DECLARE_DEFERRED(base_detect_tick);

static void base_detect_tick(void)
{
	static bool debouncing;
	int mv = adc_read_channel(ADC_BASE_DET);

	if (mv >= DETACH_MIN_THRESHOLD_MV && base_get_state()) {
		if (!debouncing) {
			debouncing = true;
		} else {
			debouncing = false;
			base_update(false);
		}
	} else if ((mv <= ATTACH_MAX_THRESHOLD_MV && !base_get_state()) ||
		   (IS_ENABLED(CONFIG_GERALT_LID_DETECTION_SELECTED) &&
		    power_status_change)) {
		if (!debouncing) {
			debouncing = true;
		} else {
			debouncing = false;
			base_update(true);
		}
	} else {
		debouncing = false;
	}
	hook_call_deferred(&base_detect_tick_data, BASE_DETECT_INTERVAL);
}

static void base_detect_enable(bool enable)
{
	if (enable) {
		hook_call_deferred(&base_detect_tick_data,
				   BASE_DETECT_INTERVAL);
	} else {
		hook_call_deferred(&base_detect_tick_data, -1);
		base_update(false);
	}
}

static void base_startup_hook(struct ap_power_ev_callback *cb,
			      struct ap_power_ev_data data)
{
	switch (data.event) {
	case AP_POWER_STARTUP:
		base_detect_enable(true);
		if (IS_ENABLED(CONFIG_GERALT_LID_DETECTION_SELECTED)) {
			power_status_change = POWER_STATUS_STARTUP;
		}
		break;
	case AP_POWER_SHUTDOWN:
		if (IS_ENABLED(CONFIG_GERALT_LID_DETECTION_SELECTED)) {
			power_status_change = POWER_STATUS_SHUTDOWN;
		} else {
			base_detect_enable(false);
		}
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
	if (IS_ENABLED(CONFIG_GERALT_LID_DETECTION_SELECTED)) {
		if (adc_read_channel(ADC_BASE_DET) > DETACH_MIN_THRESHOLD_MV) {
			base_update(false);
		}
		base_detect_enable(true);
	}
}
DECLARE_HOOK(HOOK_INIT, base_init_setting, HOOK_PRIO_DEFAULT);

void base_force_state(enum ec_set_base_state_cmd state)
{
	switch (state) {
	case EC_SET_BASE_STATE_ATTACH:
		base_detect_enable(false);
		base_update(true);
		break;
	case EC_SET_BASE_STATE_DETACH:
		base_detect_enable(false);
		base_update(false);
		break;
	case EC_SET_BASE_STATE_RESET:
		base_detect_enable(true);
		break;
	}
}
