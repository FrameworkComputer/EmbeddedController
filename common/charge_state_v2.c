/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery charging task and state machine.
 */

#include "battery.h"
#include "charge_state.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "printf.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)



void charger_task(void)
{
	while (1)
		task_wait_event(-1);
}


int charge_keep_power_off(void)
{
	return 0;
}


enum charge_state charge_get_state(void)
{
	return PWR_STATE_INIT;
}

uint32_t charge_get_flags(void)
{
	return 0;
}

int charge_get_percent(void)
{
	return 0;
}

int charge_temp_sensor_get_val(int idx, int *temp_ptr)
{
	return EC_ERROR_UNKNOWN;
}

int charge_want_shutdown(void)
{
	return 0;
}
