/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "driver/ioexpander/pcal6408.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/usb_mux/amd_fp5.h"
#include "driver/usb_mux/ps8743.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"
#include "usb_mux.h"

const struct i2c_port_t i2c_ports[] = {
	{
		.name = "tcpc0",
		.port = I2C_PORT_TCPC0,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_A0_C0_SCL,
		.sda = GPIO_EC_I2C_USB_A0_C0_SDA,
	},
	{
		.name = "tcpc1",
		.port = I2C_PORT_TCPC1,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USB_A1_C1_SCL,
		.sda = GPIO_EC_I2C_USB_A1_C1_SDA,
	},
	{
		.name = "charger",
		.port = I2C_PORT_CHARGER,
		.kbps = 100,
		.scl = GPIO_EC_I2C_POWER_SCL,
		.sda = GPIO_EC_I2C_POWER_SDA,
	},
	{
		.name = "ap_mux",
		.port = I2C_PORT_USB_AP_MUX,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USBC_AP_MUX_SCL,
		.sda = GPIO_EC_I2C_USBC_AP_MUX_SDA,
	},
	{
		.name = "thermal",
		.port = I2C_PORT_THERMAL_AP,
		.kbps = 400,
		.scl = GPIO_FCH_SIC,
		.sda = GPIO_FCH_SID,
	},
	{
		.name = "sensor",
		.port = I2C_PORT_SENSOR,
		.kbps = 400,
		.scl = GPIO_EC_I2C_SENSOR_CBI_SCL,
		.sda = GPIO_EC_I2C_SENSOR_CBI_SDA,
	},
	{
		.name = "ap_audio",
		.port = I2C_PORT_AP_AUDIO,
		.kbps = 400,
		.scl = GPIO_I2C_AUDIO_USB_HUB_SCL,
		.sda = GPIO_I2C_AUDIO_USB_HUB_SDA,
	},
	{
		.name = "battery",
		.port = I2C_PORT_BATTERY_V1,
		.kbps = 100,
		.scl = GPIO_EC_I2C_BATT_SCL,
		.sda = GPIO_EC_I2C_BATT_SDA,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/*****************************************************************************
 * IO expander
 */

struct ioexpander_config_t ioex_config[] = {
	[IOEX_C0_NCT3807] = {
		.i2c_host_port = I2C_PORT_TCPC0,
		.i2c_slave_addr = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
	},
	[IOEX_C1_NCT3807] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_slave_addr = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
		.flags = IOEX_FLAGS_DISABLED,
	},
	[IOEX_HDMI_PCAL6408] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_slave_addr = PCAL6408_I2C_ADDR0,
		.drv = &pcal6408_ioexpander_drv,
		.flags = IOEX_FLAGS_DISABLED,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ioex_config) == CONFIG_IO_EXPANDER_PORT_COUNT);

/*****************************************************************************
 * USB-A
 */

int usb_port_enable[USBA_PORT_COUNT] = {
	IOEX_EN_USB_A0_5V,
	IOEX_EN_USB_A1_5V_DB_OPT1,
};

static void usba_retimer_on(void)
{
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, usba_retimer_on, HOOK_PRIO_DEFAULT);

static void usba_retimer_off(void)
{
	ioex_set_level(IOEX_USB_A1_RETIMER_EN, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, usba_retimer_off, HOOK_PRIO_DEFAULT);

/*****************************************************************************
 * USB-C
 */

/*
 * USB C0 port SBU mux use standalone FSUSB42UMX
 * chip and it need a board specific driver.
 * Overall, it will use chained mux framework.
 */
static int fsusb42umx_set_mux(const struct usb_mux *me, mux_state_t mux_state)
{
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		ioex_set_level(IOEX_USB_C0_SBU_FLIP, 1);
	else
		ioex_set_level(IOEX_USB_C0_SBU_FLIP, 0);

	return EC_SUCCESS;
}

/*
 * .init is not necessary here because it has nothing
 * to do. Primary mux will handle mux state so .get is
 * not needed as well. usb_mux.c can handle the situation
 * properly.
 */
const struct usb_mux_driver usbc0_sbu_mux_driver = {
	.set = fsusb42umx_set_mux,
};

/*
 * Since FSUSB42UMX is not a i2c device, .i2c_port and
 * .i2c_addr_flags are not required here.
 */
const struct usb_mux usbc0_sbu_mux = {
	.usb_port = USBC_PORT_C0,
	.driver = &usbc0_sbu_mux_driver,
};

static int usbc1_hpd_set_mux(const struct usb_mux *me, mux_state_t mux_state)
{
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		/* Enable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 1);
	else
		/* Disable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 0);

	return EC_SUCCESS;
}

const struct usb_mux_driver usbc1_hpd_mux_driver = {
	.set = usbc1_hpd_set_mux,
};

const struct usb_mux usbc1_hpd_mux = {
	.usb_port = USBC_PORT_C1,
	.driver = &usbc1_hpd_mux_driver,
};

struct usb_mux usbc1_amd_fp5_usb_mux = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_USB_AP_MUX,
	.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
	.driver = &amd_fp5_usb_mux_driver,
	.flags = USB_MUX_FLAG_SET_WITHOUT_FLIP,
	.next_mux = &usbc1_hpd_mux,
};

struct usb_mux usb_muxes[] = {
	[USBC_PORT_C0] = {
		.usb_port = USBC_PORT_C0,
		.i2c_port = I2C_PORT_USB_AP_MUX,
		.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
		.driver = &amd_fp5_usb_mux_driver,
		.next_mux = &usbc0_sbu_mux,
	},
	[USBC_PORT_C1] = {
		.usb_port = USBC_PORT_C1,
		.i2c_port = I2C_PORT_TCPC1,
		.i2c_addr_flags = PS8743_I2C_ADDR1_FLAG,
		.driver = &ps8743_usb_mux_driver,
		.next_mux = &usbc1_amd_fp5_usb_mux,
	}
};
BUILD_ASSERT(ARRAY_SIZE(usb_muxes) == USBC_PORT_COUNT);

