/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio.h"
#include "ec_commands.h"
#include "host_command.h"
#include "host_command_customization.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ## args)

/*****************************************************************************/
/* Hooks */
/**
 * Notify enter/exit flash through a host command
 */
static enum ec_status flash_notified(struct host_cmd_handler_args *args)
{

	const struct ec_params_flash_notified *p = args->params;

	switch (p->flags) {
	case FLASH_EC_FIRMWARE:
	case FLASH_BIOS_FIRMWARE:
		CPRINTS("Start flashing EC firmware, disable power button and Lid");
		gpio_disable_interrupt(GPIO_ON_OFF_BTN_L);
		gpio_disable_interrupt(GPIO_ON_OFF_FP_L);
		gpio_disable_interrupt(GPIO_LID_SW_L);
		break;
	case FLASH_PD_FIRMWARE:
		gpio_disable_interrupt(GPIO_EC_PD_INTA_L);
		gpio_disable_interrupt(GPIO_EC_PD_INTB_L);
		CPRINTS("Start flashing PD firmware, lock the SMBUS");
		break;
	case FLASH_EC_DONE:
	case FLASH_BIOS_DONE:
		CPRINTS("Flash done, recover the power button, lid");
		gpio_enable_interrupt(GPIO_ON_OFF_BTN_L);
		gpio_enable_interrupt(GPIO_ON_OFF_FP_L);
		gpio_enable_interrupt(GPIO_LID_SW_L);
		break;
	case FLASH_PD_DONE:
		CPRINTS("Flash done, recover the SMBUS");
		gpio_enable_interrupt(GPIO_EC_PD_INTA_L);
		gpio_enable_interrupt(GPIO_EC_PD_INTB_L);
		break;
	default:
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_NOTIFIED, flash_notified,
			EC_VER_MASK(0));
