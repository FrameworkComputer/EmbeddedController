/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"

void gpio_pin_ctrl1_reg_write(uint32_t pin, uint32_t data)
{
	volatile struct GPIO_PIN_CTRL1_Type *gpio_ptrl1_reg;

	gpio_ptrl1_reg =
		(struct GPIO_PIN_CTRL1_Type *)GPIO_PIN_CONTROL1_ADDR(pin);
	gpio_ptrl1_reg->GPIO_PIN_CONTROL1 = data;
}
