/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */
#include "battery.h"
#include "battery_smart.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHUTDOWN_DATA	0x0010

static enum battery_present batt_pres_prev = BP_NOT_SURE;

/* Battery may delay reporting battery present */
static int battery_report_present = 1;

static const struct battery_info info = {
	.voltage_max = 13200, /* mV */
	.voltage_normal = 11550,
	.voltage_min = 9000,
	.precharge_current = 256, /* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c = 0,
	.charging_max_c = 60,
	.discharging_min_c = -20,
	.discharging_max_c = 75,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

int board_cut_off_battery(void)
{
	int rv;
	/* Ship mode command must be sent twice to take effect */
	rv = sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
	if (rv != EC_SUCCESS)
		return rv;
	return sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
}

enum battery_present battery_hw_present(void)
{
	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(GPIO_EC_BATT_PRES_ODL) ? BP_NO : BP_YES;
}

static int battery_init(void)
{
	int batt_status;

	return battery_status(&batt_status) ? 0 :
		!!(batt_status & STATUS_INITIALIZED);
}

/* Allow booting now that the battery has woke up */
static void battery_now_present(void)
{
	CPRINTS("battery will now report present");
	battery_report_present = 1;
}
DECLARE_DEFERRED(battery_now_present);

static int battery_check_disconnect(void)
{
	/* TODO(ecgh): Read the status of charge/discharge FETs */
	return BATTERY_NOT_DISCONNECTED;
}

enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres;
	static int battery_report_present_timer_started;

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	/*
	 * Make sure battery status is implemented, I2C transactions are
	 * success & the battery status is Initialized to find out if it
	 * is a working battery and it is not in the cut-off mode.
	 */
	if (batt_pres == BP_YES && batt_pres_prev != batt_pres &&
	    (battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL ||
	     battery_check_disconnect() != BATTERY_NOT_DISCONNECTED ||
	     battery_init() == 0)) {
		battery_report_present = 0;
		/*
		 * When this path is taken, the _timer_started flag must be
		 * reset so the 'else if' path will be entered and the
		 * battery_report_present flag can be set by the deferred
		 * call. This handles the case of the battery being disconected
		 * and reconnected while running or if battery_init() returns an
		 * error due to a failed sb_read.
		 */
		battery_report_present_timer_started = 0;
	}  else if (batt_pres == BP_YES && batt_pres_prev == BP_NO &&
		   !battery_report_present_timer_started) {
		/*
		 * Wait 1/2 second before reporting present if it was
		 * previously reported as not present
		 */
		battery_report_present_timer_started = 1;
		battery_report_present = 0;
		hook_call_deferred(&battery_now_present_data, 500 * MSEC);
	}

	if (!battery_report_present)
		batt_pres = BP_NO;

	batt_pres_prev = batt_pres;

	return batt_pres;
}
