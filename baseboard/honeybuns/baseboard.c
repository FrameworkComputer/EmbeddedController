/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Honeybuns family-specific configuration */
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "timer.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/******************************************************************************/
__overridable const struct power_seq board_power_seq[] = { };

__overridable const size_t board_power_seq_count =
	ARRAY_SIZE(board_power_seq);

static void board_power_sequence(void)
{
	int i;

	for(i = 0; i < board_power_seq_count; i++) {
		gpio_set_level(board_power_seq[i].signal,
			       board_power_seq[i].level);
		msleep(board_power_seq[i].delay_ms);
	}
}

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{"usbc",   I2C_PORT_USBC,   400, GPIO_EC_I2C1_SCL, GPIO_EC_I2C1_SDA},
	{"usb_mst",  I2C_PORT_MST,  400, GPIO_EC_I2C2_SCL, GPIO_EC_I2C2_SDA},
	{"eeprom",  I2C_PORT_EEPROM,  400, GPIO_EC_I2C3_SCL, GPIO_EC_I2C3_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

static void baseboard_init(void)
{
	/* Turn on power rails */
	board_power_sequence();
	CPRINTS("board: Power rails enabled");
}
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_DEFAULT);
