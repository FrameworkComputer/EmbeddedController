/*
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "chipset.h"
#include "gpio.h"
#include "ec_commands.h"
#include "host_command.h"
#include "host_command_customization.h"
#include "baseboard_host_commands.h"
#include "hooks.h"
#include "keyboard_customization.h"
#include "lid_switch.h"
#include "lpc.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "cypress5525.h"
#include "board.h"
#include "ps2mouse.h"
#include "keyboard_8042_sharedlib.h"
#include "diagnostics.h"
#include "driver/als_cm32183.h"
#include "cpu_power.h"
#include "flash_storage.h"
#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ## args)


/*****************************************************************************/
/* Hooks */

static enum ec_status privacy_switches_check(struct host_cmd_handler_args *args)
{
	struct ec_response_privacy_switches_check *r = args->response;

	/*
	 * Camera is low when off, microphone is high when off
	 * Return 0 when off/close and 1 when high/open
	 */
	r->microphone = !gpio_get_level(GPIO_MIC_SW);
	r->camera = gpio_get_level(GPIO_CAM_SW);

	CPRINTS("Microphone switch open: %d", r->microphone);
	CPRINTS("Camera switch open: %d", r->camera);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;

}
DECLARE_HOST_COMMAND(EC_CMD_PRIVACY_SWITCHES_CHECK_MODE, privacy_switches_check, EC_VER_MASK(0));
