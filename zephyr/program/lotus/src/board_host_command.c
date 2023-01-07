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
#include "power_sequence.h"

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

static enum ec_status enter_non_acpi_mode(struct host_cmd_handler_args *args)
{
	/**
	 * TODO:
	 * When system boot into OS, host will call this command to verify,
	 * It means system should in S0 state, and we need to set the resume
	 * S0ix flag to avoid the wrong state when unknown reason warm boot.
	 * if (chipset_in_state(CHIPSET_STATE_STANDBY))
	 *	*host_get_customer_memmap(EC_EMEMAP_ER1_POWER_STATE) |= EC_PS_RESUME_S0ix;
	 */

	/**
	 * When system reboot and go into setup menu, we need to set the power_s5_up flag
	 * to wait SLP_S5 and SLP_S3 signal to boot into OS.
	 */
	power_s5_up_control(1);

	/**
	 * TODO: clear ENTER_S4/S5 flag
	 * power_state_clear(EC_PS_ENTER_S4 | EC_PS_RESUME_S4 |
	 *	EC_PS_ENTER_S5 | EC_PS_RESUME_S5);
	 */

	/**
	 * TODO: clear ACPI ready flags for pre-os
	 * *host_get_customer_memmap(0x00) &= ~BIT(0);
	 */

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_NON_ACPI_NOTIFY, enter_non_acpi_mode, EC_VER_MASK(0));

static enum ec_status enter_acpi_mode(struct host_cmd_handler_args *args)
{
	/**
	 * TODO:
	 * Moved sci enable on this host command, we need to check acpi_driver ready flag
	 * every boot up (both cold boot and warn boot)
	 * hook_call_deferred(&sci_enable_data, 250 * MSEC);
	 */

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_ACPI_NOTIFY, enter_acpi_mode, EC_VER_MASK(0));
