/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Arcada ISH board-specific configuration */

#include "console.h"
#include "gpio.h"
#include "host_command.h"
#include "i2c.h"

#include "gpio_list.h" /* has to be included last */
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* I2C port map */
const struct i2c_port_t i2c_ports[]  = {
	{"sensor", I2C_PORT_SENSOR,   1000, GPIO_ISH_I2C0_SCL,   GPIO_ISH_I2C0_SDA}
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* dummy functions to remove 'undefined' symbol link error for acpi.o
 * due to CONFIG_LPC flag
 */
#ifdef CONFIG_HOSTCMD_LPC
int lpc_query_host_event_state(void)
{
	return 0;
}

void lpc_set_acpi_status_mask(uint8_t mask)
{
}
#endif
