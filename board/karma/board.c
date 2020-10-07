/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio.h"
#include "oz554.h"

#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

__override void oz554_board_init(void)
{
	int pin_status = 0;

	pin_status |= gpio_get_level(GPIO_PANEL_ID_0) << 0;
	pin_status |= gpio_get_level(GPIO_PANEL_ID_1) << 1;
	pin_status |= gpio_get_level(GPIO_PANEL_ID_2) << 2;

	switch (pin_status) {
	case 0x04:
		CPRINTS("PANEL_LM_SSE2");
		break;
	case 0x05:
		CPRINTS("PANEL_LM_SSK1");
		/* Reigster 0x02: Setting LED current: 55(mA) */
		if (oz554_set_config(2, 0x55))
			CPRINTS("oz554 config failed");
		break;
	case 0x06:
		CPRINTS("PANEL_LM_SSM1");
		if (oz554_set_config(2, 0x46))
			CPRINTS("oz554 config failed");
		if (oz554_set_config(5, 0x87))
			CPRINTS("oz554 config failed");
		break;
	default:
		CPRINTS("PANEL_UNKNOWN");
		break;
	}
}
