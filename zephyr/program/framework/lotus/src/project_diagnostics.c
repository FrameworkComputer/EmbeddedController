/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "board_host_command.h"
#include "console.h"
#include "diagnostics.h"
#include "dptf.h"
#include "fan.h"
#include "gpu.h"
#include "hooks.h"
#include "input_module.h"
#include "port80.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

static void check_fan_ready_deferred(void);
DECLARE_DEFERRED(check_fan_ready_deferred);

static void check_fan_ready_deferred(void)
{
	static bool flag_right_fan_ready;
	static bool flag_left_fan_ready;
	static int count;

	if ((fan_get_rpm_actual(0) > 100) && !flag_right_fan_ready) {
		flag_right_fan_ready = true;
		set_diagnostic(DIAGNOSTICS_NO_RIGHT_FAN, false);
	}
	if ((fan_get_rpm_actual(1) > 100) && !flag_left_fan_ready) {
		flag_left_fan_ready = true;
		set_diagnostic(DIAGNOSTICS_NO_LEFT_FAN, false);
	}

	if (flag_right_fan_ready && flag_left_fan_ready) {
		/* Exit the duty mode and let thermal to control the fan */
		dptf_set_fan_duty_target(-1);
		count = 0;
		flag_right_fan_ready = 0;
		flag_left_fan_ready = 0;
		set_device_complete(true);
	} else if (count < 15) {
		count++;
		hook_call_deferred(&check_fan_ready_deferred_data, 500 * MSEC);
	} else {
		dptf_set_fan_duty_target(-1);
		count = 0;
		flag_right_fan_ready = 0;
		flag_left_fan_ready = 0;
		set_device_complete(true);
	}
}

void start_fan_deferred(void)
{
	/* force turn on the fan for diagnostic */
	dptf_set_fan_duty_target(20);
}
DECLARE_DEFERRED(start_fan_deferred);

void check_device_deferred(void)
{
	/* Clear the DIAGNOSTICS_HW_NO_BATTERY flag if battery is present */
	if (battery_is_present() == BP_YES || get_standalone_mode())
		set_diagnostic(DIAGNOSTICS_HW_NO_BATTERY, false);

	if (gpu_module_fault())
		set_diagnostic(DIAGNOSTICS_GPU_MODULE_FAULT, true);

	if (get_deck_state() != DECK_ON && !get_standalone_mode())
		set_diagnostic(DIAGNOSTICS_INPUT_MODULE_FAULT, true);

	if (amd_ddr_initialized_check())
		set_bios_diagnostic(CODE_DDR_FAIL);

	if (!get_standalone_mode())
		hook_call_deferred(&check_fan_ready_deferred_data, 0);
	else {
		set_device_complete(true);
		dptf_set_fan_duty_target(-1);
	}
}
DECLARE_DEFERRED(check_device_deferred);

void project_diagnostics(void)
{
	hook_call_deferred(&start_fan_deferred_data, 500 * MSEC);
	hook_call_deferred(&check_device_deferred_data, 2000 * MSEC);
}
