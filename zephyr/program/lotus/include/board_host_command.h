/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_HOST_COMMAND_H
#define __BOARD_HOST_COMMAND_H

/* Configure the behavior of the flash notify */
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

#endif /* __BOARD_HOST_COMMAND_H */
