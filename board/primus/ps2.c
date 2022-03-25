/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042.h"
#include "ps2_chip.h"
#include "time.h"

void send_aux_data_to_device(uint8_t data)
{
	ps2_transmit_byte(NPCX_PS2_CH1, data);
}

static void board_init(void)
{
	ps2_enable_channel(NPCX_PS2_CH1, 1, send_aux_data_to_host_interrupt);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/*
 * Goodix touchpad AVDD need to pull low to 0V when poweroff.
 * Setting PS2 module in GPIO.inc will let AVDD have 0.9V offset.
 * So we need to enable PS2 module later than PLTRST# to avoid the 0.9V
 * offset.
 */
static void enable_ps2(void)
{
	gpio_set_alternate_function(GPIO_PORT_6,
		BIT(2) | BIT(3), GPIO_ALT_FUNC_DEFAULT);
}
DECLARE_DEFERRED(enable_ps2);

static void disable_ps2(void)
{
	gpio_set_flags(GPIO_EC_PS2_SCL_TPAD, GPIO_ODR_LOW);
	gpio_set_flags(GPIO_EC_PS2_SDA_TPAD, GPIO_ODR_LOW);
	gpio_set_alternate_function(GPIO_PORT_6,
		BIT(2) | BIT(3), GPIO_ALT_FUNC_NONE);
	/* make sure PLTRST# goes high and re-enable ps2.*/
	hook_call_deferred(&enable_ps2_data, 2 * SECOND);
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, disable_ps2, HOOK_PRIO_DEFAULT);
