/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "gpio.h"
#include "registers.h"
#include "i2c.h"
#include "util.h"


/* To define the gpio_list[] instance. */
#include "gpio_list.h"

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"master", 0 /*Port*/, 100 /*Kbps*/, GPIO_MCU_SCL, GPIO_MCU_SDA},
};

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

