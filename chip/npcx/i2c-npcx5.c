/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C module driver depends on chip series for Chrome EC */

#include "i2c.h"
#include "i2c_chip.h"
#include "registers.h"
#include "util.h"

/*****************************************************************************/
/* IC specific low-level driver depends on chip series */

int i2c_port_to_controller(int port)
{
	if (port < 0 || port >= I2C_PORT_COUNT)
		return -1;

	return (port == NPCX_I2C_PORT0_0) ? 0 : port - 1;
}

void i2c_select_port(int port)
{
	/*
	 * I2C0_1 uses port 1 of controller 0. All other I2C pin sets
	 * use port 0.
	 */
	if (port > NPCX_I2C_PORT0_1)
		return;

	/* Select IO pins for multi-ports I2C controllers */
	UPDATE_BIT(NPCX_GLUE_SMBSEL, NPCX_SMBSEL_SMB0SEL,
			(port == NPCX_I2C_PORT0_1));
}

int i2c_is_raw_mode(int port)
{
	int bit = (port > NPCX_I2C_PORT0_1) ? ((port - 1) * 2) : port;

	if (IS_BIT_SET(NPCX_DEVALT(2), bit))
		return 0;
	else
		return 1;
}

