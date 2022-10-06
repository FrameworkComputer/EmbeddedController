/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "i2c.h"
#include "timer.h"

void button_event(enum gpio_signal signal)
{
	gpio_set_level(GPIO_LED_U, 1);
}

#ifdef CTS_MODULE
/*
 * Mock interrupt handler. It's supposed to be overwritten by each suite
 * if needed.
 */
__attribute__((weak)) void cts_irq1(enum gpio_signal signal)
{
}
__attribute__((weak)) void cts_irq2(enum gpio_signal signal)
{
}
#endif

#include "gpio_list.h"

void tick_event(void)
{
	static int count;

	gpio_set_level(GPIO_LED_U, (count & 0x07) == 0);

	count++;
}
DECLARE_HOOK(HOOK_TICK, tick_event, HOOK_PRIO_DEFAULT);

#ifdef CTS_MODULE_I2C
const struct i2c_port_t i2c_ports[] = {
	{ .name = "test",
	  .port = STM32_I2C1_PORT,
	  .kbps = 100,
	  .scl = GPIO_I2C1_SCL,
	  .sda = GPIO_I2C1_SDA },
};

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
#endif

/******************************************************************************
 * Initialize board.
 */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_USER_BUTTON);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
