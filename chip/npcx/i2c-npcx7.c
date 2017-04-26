/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C module driver depends on chip series for Chrome EC */

#include "common.h"
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

	if (port <= NPCX_I2C_PORT3_0)
		return port;
#ifndef NPCX_PSL_MODE_SUPPORT
	else if (port == NPCX_I2C_PORT4_0)
		return 4;
#endif
	else /* If port >= NPCX_I2C_PORT4_1 */
		return 4 + ((port - NPCX_I2C_PORT4_1 + 1) / 2);
}

void i2c_select_port(int port)
{
	/* Only I2C 4/5/6 have multiple ports in series npcx7 */
	if (port <= NPCX_I2C_PORT3_0 || port >= NPCX_I2C_PORT7_0)
		return;
	/* Select I2C ports for the same controller */
	else if (port <= NPCX_I2C_PORT4_1) {
		UPDATE_BIT(NPCX_GLUE_SMBSEL, NPCX_SMBSEL_SMB4SEL,
			(port == NPCX_I2C_PORT4_1));
	} else if (port <= NPCX_I2C_PORT5_1) {
		UPDATE_BIT(NPCX_GLUE_SMBSEL, NPCX_SMBSEL_SMB5SEL,
			(port == NPCX_I2C_PORT5_1));
	} else {
		UPDATE_BIT(NPCX_GLUE_SMBSEL, NPCX_SMBSEL_SMB6SEL,
			(port == NPCX_I2C_PORT6_1));
	}
}

int i2c_is_raw_mode(int port)
{
	int group, bit;

	if (port == NPCX_I2C_PORT4_1 || port == NPCX_I2C_PORT5_1 ||
			port == NPCX_I2C_PORT6_1) {
		group = 6;
		bit = 7 - (port - NPCX_I2C_PORT4_1) / 2;
	} else {
		group = 2;
		if (port <= NPCX_I2C_PORT3_0)
			bit = 2 * port;
		else
			bit = I2C_PORT_COUNT - port;
	}

	if (IS_BIT_SET(NPCX_DEVALT(group), bit))
		return 0;
	else
		return 1;
}
