/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "i8042_protocol.h"
#include "keyboard_8042.h"
#include "ps2_chip.h"

void send_aux_data_to_device(uint8_t data)
{
	if (data == I8042_CMD_RESET_DIS) {
		/*
		 * EC will receive I8042_CMD_RESET_DIS when warm reboot,
		 * set GPIO62/ GPIO63 back to GPIO and pull low.
		 */
		gpio_set_flags(GPIO_EC_PS2_SCL_TPAD, GPIO_ODR_LOW);
		gpio_set_flags(GPIO_EC_PS2_SDA_TPAD, GPIO_ODR_LOW);
		gpio_set_alternate_function(GPIO_PORT_6,
			BIT(2) | BIT(3), GPIO_ALT_FUNC_NONE);
	} else if (data == I8042_CMD_GETID) {
		/*
		 * In normal boot, when we get I8042_CMD_GETID command,
		 * enable the PS2 module.
		 */
		gpio_set_alternate_function(GPIO_PORT_6,
			BIT(2) | BIT(3), GPIO_ALT_FUNC_DEFAULT);
	}
	ps2_transmit_byte(NPCX_PS2_CH1, data);
}

static void board_init(void)
{
	ps2_enable_channel(NPCX_PS2_CH1, 1, send_aux_data_to_host_interrupt);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
