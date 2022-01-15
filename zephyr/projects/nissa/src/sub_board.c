/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nissa sub-board selection */

#include <device.h>
#include <drivers/cros_cbi.h>
#include <drivers/gpio.h>

#include "console.h"
#include "hooks.h"
#include "gpio/gpio_int.h"
#include "sub_board.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

enum nissa_sub_board_type nissa_get_sb_type(void)
{
	static enum nissa_sub_board_type sb = NISSA_SB_UNKNOWN;
	int ret;
	uint32_t val;
	const struct device *dev;

	/*
	 * Return cached value.
	 */
	if (sb != NISSA_SB_UNKNOWN)
		return sb;

	sb = NISSA_SB_NONE;	/* Defaults to none */
	dev = device_get_binding(CROS_CBI_LABEL);
	if (dev == NULL) {
		CPRINTS("No %s device", CROS_CBI_LABEL);
	} else {
		ret = cros_cbi_get_fw_config(dev, FW_SUB_BOARD, &val);
		if (ret != 0) {
			CPRINTS("Error retrieving CBI FW_CONFIG field %d",
				FW_SUB_BOARD);
			return sb;
		}
		switch (val) {
		default:
			CPRINTS("No sub-board defined");
			break;
		case FW_SUB_BOARD_1:
			sb = NISSA_SB_C_A;
			CPRINTS("SB: USB type C, USB type A");
			break;

		case FW_SUB_BOARD_2:
			sb = NISSA_SB_C_LTE;
			CPRINTS("SB: USB type C, WWAN LTE");
			break;

		case FW_SUB_BOARD_3:
			sb = NISSA_SB_HDMI_A;
			CPRINTS("SB: HDMI, USB type A");
			break;
		}
	}
	return sb;
}
