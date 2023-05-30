/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_host_command.h"
#include "console.h"
#include "diagnostics.h"
#include "fan.h"
#include "gpu.h"
#include "hooks.h"
#include "input_module.h"
#include "port80.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

void check_device_deferred(void)
{
	if (!gpu_present())
		set_diagnostic(DIAGNOSTICS_GPU_MODULE_FAULT, true);

	if (get_deck_state() != DECK_ON && !get_standalone_mode())
		set_diagnostic(DIAGNOSTICS_INPUT_MODULE_FAULT, true);

	if (!(fan_get_rpm_actual(0) > 100))
		set_diagnostic(DIAGNOSTICS_NO_RIGHT_FAN, true);

	if (!(fan_get_rpm_actual(1) > 100))
		set_diagnostic(DIAGNOSTICS_NO_LEFT_FAN, true);

	if (amd_ddr_initialized_check())
		set_bios_diagnostic(CODE_DDR_FAIL);
}
DECLARE_DEFERRED(check_device_deferred);

void project_diagnostics(void)
{
	hook_call_deferred(&check_device_deferred_data, 2000 * MSEC);
}
