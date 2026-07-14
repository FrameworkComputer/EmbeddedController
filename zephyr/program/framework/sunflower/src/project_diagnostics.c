/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "board_host_command.h"
#include "board_adc.h"
#include "console.h"
#include "diagnostics.h"
#include "dptf.h"
#include "driver/accel_bma422.h"
#include "driver/ioexpander/it8801.h"
#include "fan.h"
#include "hooks.h"
#include "i2c.h"
#include "input_deck.h"
#include "port80.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

void start_fan_deferred(void)
{
	if (is_device_complete())
		return;

	/* force turn on the fan for diagnostic */
	dptf_set_fan_duty_target(30);
}
DECLARE_DEFERRED(start_fan_deferred);
DECLARE_HOOK(HOOK_CHIPSET_RESET, start_fan_deferred, HOOK_PRIO_DEFAULT);

void check_device_deferred(void)
{
	int audio = get_hardware_id(ADC_AUDIO_ID);
	int powerbtn = get_hardware_id(ADC_POWER_BUTTON_BOARD_ID);
	bool pb_enable = (powerbtn >= BOARD_VERSION_1 && powerbtn <= BOARD_VERSION_13);
	int bma4_id = 0;

	/* Clear the DIAGNOSTICS_HW_NO_BATTERY flag if battery is present */
	if (battery_is_present() == BP_YES || get_standalone_mode())
		set_diagnostic(DIAGNOSTICS_HW_NO_BATTERY, false);

#ifndef CONFIG_PLATFORM_IGNORED_TOUCHPAD_ID
	if (!input_deck_is_present() && !get_standalone_mode())
		set_diagnostic(DIAGNOSTICS_INPUT_COVER, true);
#endif

	if ((audio <= BOARD_VERSION_1 || audio >= BOARD_VERSION_14) &&
		!get_standalone_mode())
		set_diagnostic(DIAGNOSTICS_AUDIO_DAUGHTERBOARD, true);

	if (!pb_enable) {
		CPRINTS("Power button board missing");
	}

	/* Check whether the lid accelerometer responds */
	i2c_read8(I2C_PORT_MOTION_SENSOR, BMA4_I2C_ADDR_SECONDARY,
				BMA4_CHIP_ID_ADDR, &bma4_id);
	if (bma4_id != BMA422_CHIP_ID || get_standalone_mode()) {
		set_diagnostic(DIAGNOSTICS_CAMERA_MODULE, true);
		CPRINTS("Lid accelerometer missing");
	}

	if (!(fan_get_rpm_actual(0) > 100))
		set_diagnostic(DIAGNOSTICS_NOFAN, true);

	/* Exit the duty mode and let thermal to control the fan */
	dptf_set_fan_duty_target(-1);

	set_device_complete(true);
}
DECLARE_DEFERRED(check_device_deferred);

void project_diagnostics(void)
{
	hook_call_deferred(&start_fan_deferred_data, 500 * MSEC);
	hook_call_deferred(&check_device_deferred_data, 2000 * MSEC);
}
