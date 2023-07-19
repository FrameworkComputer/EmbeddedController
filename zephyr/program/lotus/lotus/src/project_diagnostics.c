/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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

void start_fan_deferred(void)
{
	/* force turn on the fan for diagnostic */
	dptf_set_fan_duty_target(5);
}
DECLARE_DEFERRED(start_fan_deferred);

void check_device_deferred(void)
{
	if (gpu_module_fault())
		set_diagnostic(DIAGNOSTICS_GPU_MODULE_FAULT, true);

	if (get_deck_state() != DECK_ON && !get_standalone_mode())
		set_diagnostic(DIAGNOSTICS_INPUT_MODULE_FAULT, true);

	/* force turn on the fan for diagnostic */
	dptf_set_fan_duty_target(5);

	if (!(fan_get_rpm_actual(0) > 100) && !get_standalone_mode())
		set_diagnostic(DIAGNOSTICS_NO_RIGHT_FAN, true);

	if (!(fan_get_rpm_actual(1) > 100) && !get_standalone_mode())
		set_diagnostic(DIAGNOSTICS_NO_LEFT_FAN, true);

	/* Exit the duty mode and let thermal to control the fan */
	dptf_set_fan_duty_target(-1);

	if (amd_ddr_initialized_check())
		set_bios_diagnostic(CODE_DDR_FAIL);
}
DECLARE_DEFERRED(check_device_deferred);

void project_diagnostics(void)
{
	hook_call_deferred(&start_fan_deferred_data, 500 * MSEC);
	hook_call_deferred(&check_device_deferred_data, 2000 * MSEC);
}
