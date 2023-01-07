/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_HOST_COMMAND_H
#define __BOARD_HOST_COMMAND_H

#include "host_command.h"

/*****************************************************************************/
/*
 * Configure the behavior of the flash notify
 */
#define EC_CMD_FLASH_NOTIFIED		0x3E01

enum ec_flash_notified_flags {
	/* Enable/Disable power button pulses for x86 devices */
	FLASH_ACCESS_SPI	= 0,
	FLASH_FIRMWARE_START	= 1,
	FLASH_FIRMWARE_DONE	= 2,
	FLASH_ACCESS_SPI_DONE	= 3,
	FLASH_FLAG_PD		= BIT(4),
};

struct ec_params_flash_notified {
	/* See enum ec_flash_notified_flags */
	uint8_t flags;
} __ec_align1;

/*****************************************************************************/
/*
 * Configure the behavior of the charge limit control
 */
#define EC_CMD_CHARGE_LIMIT_CONTROL	0x3E03
#define EC_CHARGE_LIMIT_RESTORE		0x7F

enum ec_chg_limit_control_modes {
	/* Disable all setting, charge control by charge_manage */
	CHG_LIMIT_DISABLE	= BIT(0),
	/* Set maximum and minimum percentage */
	CHG_LIMIT_SET_LIMIT	= BIT(1),
	/* Host read current setting */
	CHG_LIMIT_GET_LIMIT	= BIT(3),
	/* Enable override mode, allow charge to full this time */
	CHG_LIMIT_OVERRIDE	= BIT(7),
};

struct ec_params_ec_chg_limit_control {
	/* See enum ec_chg_limit_control_modes */
	uint8_t modes;
	uint8_t max_percentage;
	uint8_t min_percentage;
} __ec_align1;

struct ec_response_chg_limit_control {
	uint8_t max_percentage;
	uint8_t min_percentage;
} __ec_align1;

/*****************************************************************************/
/*
 * This command uses to notify the EC that the system is in non-ACPI mode
 */
#define EC_CMD_NON_ACPI_NOTIFY		0x3E07

/*****************************************************************************/
/*
 * This command uses to notify the EC that the system is in ACPI mode
 */
#define EC_CMD_ACPI_NOTIFY		0x3E10

#endif /* __BOARD_HOST_COMMAND_H */
