/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MAX32660 EvalKit Board Specific Configuration */

#include "i2c.h"
#include "board.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "timer.h"
#include "registers.h"
#include "util.h"
#include "gpio_list.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {};

const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
