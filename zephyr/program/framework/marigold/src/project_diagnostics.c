/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "board_host_command.h"
#include "board_adc.h"
#include "console.h"
#include "diagnostics.h"
#include "dptf.h"
#include "driver/temp_sensor/f75303.h"
#include "fan.h"
#include "hooks.h"
#include "i2c.h"
#include "port80.h"
#include "temp_sensor/temp_sensor.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

#define THERMAL_UN2_ADDR_FLAG_4D 0x4D
#define F75303_PRODUCT_ID_ADDR 0xFD
#define F75303_ID	0x21
#define NCT7719W_PRODUCT_ID_ADDR 0xFE
#define NCT7719W_ID 0x5C

#define THERMAL_UN3_ADDR_FLAG_4C 0x4C
#define UN3_PRODUCT_ID_ADDR 0xFE
#define F75397_ID 0x50
#define G788P81U_ID 0x47
#define NCT7718W_ID 0x50


void start_fan_deferred(void)
{
	/* force turn on the fan for diagnostic */
	dptf_set_fan_duty_target(20);
}
DECLARE_DEFERRED(start_fan_deferred);

void check_device_deferred(void)
{
#ifdef CONFIG_PLATFORM_IGNORED_TOUCHPAD_ID
	int touchpad = BOARD_VERSION_10;
#else
	int touchpad = get_hardware_id(ADC_TOUCHPAD_ID);
#endif /* CONFIG_PLATFORM_IGNORED_TOUCHPAD_ID */
	int audio = get_hardware_id(ADC_AUDIO_ID);
	int product_id;

	/* Clear the DIAGNOSTICS_HW_NO_BATTERY flag if battery is present */
	if (battery_is_present() == BP_YES || get_standalone_mode())
		set_diagnostic(DIAGNOSTICS_HW_NO_BATTERY, false);

	if ((touchpad < BOARD_VERSION_1 || touchpad >= BOARD_VERSION_14) &&
		!get_standalone_mode())
		set_diagnostic(DIAGNOSTICS_TOUCHPAD, true);

	if ((audio <= BOARD_VERSION_1 || audio >= BOARD_VERSION_14) &&
		!get_standalone_mode())
		set_diagnostic(DIAGNOSTICS_AUDIO_DAUGHTERBOARD, true);

	i2c_read8(I2C_PORT_SENSOR, THERMAL_UN2_ADDR_FLAG_4D, F75303_PRODUCT_ID_ADDR, &product_id);
	if (product_id != F75303_ID) {
		i2c_read8(I2C_PORT_SENSOR, THERMAL_UN2_ADDR_FLAG_4D, NCT7719W_PRODUCT_ID_ADDR,
					 &product_id);
		if (product_id != NCT7719W_ID)
			set_diagnostic(DIAGNOSTICS_THERMAL_SENSOR, true);
	}

	i2c_read8(I2C_PORT_SENSOR, THERMAL_UN3_ADDR_FLAG_4C, UN3_PRODUCT_ID_ADDR, &product_id);
	if (product_id != F75397_ID && product_id != G788P81U_ID && product_id != NCT7718W_ID)
		set_diagnostic(DIAGNOSTICS_THERMAL_SENSOR, true);

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
