/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "i2c.h"

#ifdef CTS_MODULE
/*
 * Mock interrupt handler. It's supposed to be overwritten by each suite
 * if needed.
 */
__attribute__((weak)) void cts_irq(enum gpio_signal signal)
{
}
#endif

#include "gpio_list.h"

void tick_event(void)
{
	static int count;

	gpio_set_level(GPIO_LED_GREEN, (count & 0x03) == 0);

	count++;
}
DECLARE_HOOK(HOOK_TICK, tick_event, HOOK_PRIO_DEFAULT);

#ifdef CTS_MODULE_I2C
const struct i2c_port_t i2c_ports[]  = {
	{"test", STM32_I2C2_PORT, 100, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
#endif
