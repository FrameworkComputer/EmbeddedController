/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

#include "board_host_command.h"
#include "console.h"
#include "cypress_pd_common.h"
#include "ec_commands.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "host_command.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_HOSTCMD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_HOSTCMD, format, ##args)

static enum ec_status flash_notified(struct host_cmd_handler_args *args)
{

	const struct ec_params_flash_notified *p = args->params;

	switch (p->flags & 0x03) {
	case FLASH_FIRMWARE_START:
		CPRINTS("Start flashing firmware, flags:0x%02x", p->flags);
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_powerbtn));
		
		/**
		 * TODO: After LID switch function is impelmented, disable the
		 * interrupt fo the lid switch pin
		 * gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(lid_sw));
		 */

		if ((p->flags & FLASH_FLAG_PD) == FLASH_FLAG_PD) {
			gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip0_interrupt));
			gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip1_interrupt));
			set_pd_fw_update(true);
		}
	case FLASH_ACCESS_SPI:
		break;

	case FLASH_FIRMWARE_DONE:
		CPRINTS("Flash done, flags:0x%02x", p->flags);
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_powerbtn));
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip0_interrupt));
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip1_interrupt));

		/**
		 * TODO: After LID switch function is impelmented, enable the
		 * interrupt fo the lid switch pin
		 * gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(lid_sw));
		 */

		set_pd_fw_update(false);
		/* resetup PD controllers */
		if ((p->flags & FLASH_FLAG_PD) == FLASH_FLAG_PD)
			cypd_reinitialize();

	case FLASH_ACCESS_SPI_DONE:
		break;
	default:
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_NOTIFIED, flash_notified,
			EC_VER_MASK(0));
