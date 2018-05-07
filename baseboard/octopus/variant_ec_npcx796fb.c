/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code for VARIANT_OCTOPUS_EC_NPCX796FB configuration */

#include "charge_manager.h"
#include "chipset.h"
#include "gpio.h"
#include "i2c.h"
#include "power.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"
#include "timer.h"

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{"battery", I2C_PORT_BATTERY, 100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"tcpc0",   I2C_PORT_TCPC0,   100, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"tcpc1",   I2C_PORT_TCPC1,   100, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"eeprom",  I2C_PORT_EEPROM,  100, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
	{"charger", I2C_PORT_CHARGER, 100, GPIO_I2C4_SCL, GPIO_I2C4_SDA},
	{"sensor",  I2C_PORT_SENSOR,  100, GPIO_I2C7_SCL, GPIO_I2C7_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#define HIBERNATE_VBUS_LEVEL_MV	5000

void board_hibernate(void)
{
	int port;

	/*
	 * To support hibernate called from console commands, ectool commands
	 * and key sequence, shutdown the AP before hibernating.
	 */
	chipset_force_shutdown();

	/*
	 * If we are charging, then drop the Vbus level down to 5V to ensure
	 * that we don't get locked out of the 6.8V OVLO for our PPCs in
	 * dead-battery mode. This is needed when the TCPC/PPC rails go away.
	 * (b/79218851)
	 */
	port = charge_manager_get_active_charge_port();
	if (port != CHARGE_PORT_NONE)
		pd_request_source_voltage(port, HIBERNATE_VBUS_LEVEL_MV);

	/*
	 * Delay allows AP power state machine to settle down along
	 * with any PD contract renegotiation.
	 */
	msleep(100);

	for (port = 0; port < CONFIG_USB_PD_PORT_COUNT; port++) {
		/*
		 * If Vbus isn't already on this port, then open the SNK path
		 * to allow AC to pass through to the charger when connected.
		 * This is need if the TCPC/PPC rails do not go away.
		 * (b/79173959)
		 */
		if (!pd_is_vbus_present(port))
			ppc_vbus_sink_enable(port, 1);
	}
}
