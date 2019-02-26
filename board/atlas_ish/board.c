/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Atlas ISH board-specific configuration */

#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "math_util.h"
#include "task.h"
#include "uart.h"

#include "gpio_list.h" /* has to be included last */
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* I2C port map */
const struct i2c_port_t i2c_ports[]  = {
	{"trackpad", I2C_PORT_TP, 1000,
	GPIO_I2C_PORT_TP_SCL, GPIO_I2C_PORT_TP_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
