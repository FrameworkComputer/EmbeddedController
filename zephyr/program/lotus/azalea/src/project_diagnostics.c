/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_host_command.h"
#include "board_adc.h"
#include "console.h"
#include "diagnostics.h"
#include "driver/temp_sensor/f75303.h"
#include "fan.h"
#include "hooks.h"
#include "port80.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)


void check_device_deferred(void)
{
	int touchpad = get_hardware_id(ADC_TOUCHPAD_ID);
	int audio = get_hardware_id(ADC_AUDIO_ID);
	int temps;


	if (touchpad <= BOARD_VERSION_1 || touchpad >= BOARD_VERSION_14)
		set_diagnostic(DIAGNOSTICS_TOUCHPAD, true);

	if (audio <= BOARD_VERSION_1 || audio >= BOARD_VERSION_14)
		set_diagnostic(DIAGNOSTICS_AUDIO_DAUGHTERBOARD, true);

	f75303_get_val(F75303_IDX_LOCAL, &temps);
	if (temps == 0)
		set_diagnostic(DIAGNOSTICS_THERMAL_SENSOR, true);


	if (!(fan_get_rpm_actual(0) > 100))
		set_diagnostic(DIAGNOSTICS_NOFAN, true);

	if (amd_ddr_initialized_check())
		set_bios_diagnostic(CODE_DDR_FAIL);
}
DECLARE_DEFERRED(check_device_deferred);

void project_diagnostics(void)
{
	hook_call_deferred(&check_device_deferred_data, 2000 * MSEC);
}
