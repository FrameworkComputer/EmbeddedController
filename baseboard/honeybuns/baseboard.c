/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Honeybuns family-specific configuration */
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "usb_pd.h"
#include "system.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)

/******************************************************************************/

static void board_power_sequence(void)
{
	int i;

	for(i = 0; i < board_power_seq_count; i++) {
		gpio_set_level(board_power_seq[i].signal,
			       board_power_seq[i].level);
		if (board_power_seq[i].delay_ms)
			msleep(board_power_seq[i].delay_ms);
	}
}

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{"i2c1",  I2C_PORT_I2C1,  400, GPIO_EC_I2C1_SCL, GPIO_EC_I2C1_SDA},
	{"i2c3",  I2C_PORT_I2C3,  400, GPIO_EC_I2C3_SCL, GPIO_EC_I2C3_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

static void baseboard_init(void)
{
	/* Turn on power rails */
	board_power_sequence();
	CPRINTS("board: Power rails enabled");

#ifdef SECTION_IS_RW
	/* Force TC state machine to start in TC_ERROR_RECOVERY */
	system_clear_reset_flags(EC_RESET_FLAG_POWER_ON);
	/* Make certain SN5S330 PPC does full initialization */
	system_set_reset_flags(EC_RESET_FLAG_EFS);
#else
	/* Set up host port usbc to present Rd on CC lines */
	if(baseboard_usbc_init(USB_PD_PORT_HOST))
		CPRINTS("usbc: Failed to set up sink path");
#endif
}
/*
 * Power sequencing must run before any other chip init is attempted, so run
 * power sequencing as soon as I2C bus is initialized.
 */
DECLARE_HOOK(HOOK_INIT, baseboard_init, HOOK_PRIO_INIT_I2C + 1);
