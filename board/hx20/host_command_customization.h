/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* host command customization configuration */

#ifndef __HOST_COMMAND_CUSTOMIZATION_H
#define __HOST_COMMAND_CUSTOMIZATION_H

/*****************************************************************************/
/* Configure the behavior of the flash notify */
#define EC_CMD_FLASH_NOTIFIED 0x3E01

enum ec_flash_notified_flags {
	/* Enable/Disable power button pulses for x86 devices */
	FLASH_FIRMWARE_START  = BIT(0),
	FLASH_FIRMWARE_DONE   = BIT(1),
	FLASH_FLAG_PD         = BIT(4),
};

struct ec_params_flash_notified {
	/* See enum ec_config_power_button_flags */
	uint8_t flags;
} __ec_align1;

#endif /* __HOST_COMMAND_CUSTOMIZATION_H */