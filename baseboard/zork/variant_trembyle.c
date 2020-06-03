/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/charger/isl9241.h"
#include "driver/retimer/ps8802.h"
#include "driver/retimer/ps8818.h"
#include "driver/retimer/tusb544.h"
#include "driver/tcpm/nct38xx.h"
#include "driver/usb_mux/amd_fp5.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "ioexpander.h"
#include "timer.h"
#include "usb_mux.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ## args)

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
		.name = "battery",
		.port = I2C_PORT_BATTERY,
		.kbps = 100,
		.scl = GPIO_EC_I2C_BATT_SCL,
		.sda = GPIO_EC_I2C_BATT_SDA,
	},
	{
		.name = "ap_mux",
		.port = I2C_PORT_USB_AP_MUX,
		.kbps = 400,
		.scl = GPIO_EC_I2C_USBC_AP_MUX_SCL,
		.sda = GPIO_EC_I2C_USBC_AP_MUX_SDA,
	},
	{
		.name = "therm_chg",
		.port = I2C_PORT_THERMAL_AP,
		.kbps = 400,
		.scl = GPIO_FCH_SIC_POWER_SCL,
		.sda = GPIO_FCH_SID_POWER_SDA,
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
		.scl = GPIO_FCH_I2C_AUDIO_SCL,
		.sda = GPIO_FCH_I2C_AUDIO_SDA,
	},
	{
		.name = "ap_hdmi",
		.port = I2C_PORT_AP_HDMI,
		.kbps = 400,
		.scl = GPIO_FCH_I2C_HDMI_HUB_3V3_SCL,
		.sda = GPIO_FCH_I2C_HDMI_HUB_3V3_SDA,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/*****************************************************************************
 * Charger
 */

struct charger_config_t chg_chips[] = {
	{
		.i2c_port = I2C_PORT_CHARGER_V1,
		.i2c_addr_flags = ISL9241_ADDR_FLAGS,
		.drv = &isl9241_drv,
	},
};
const unsigned int chg_cnt = ARRAY_SIZE(chg_chips);

/*
 * If the charger is found on the V0 I2C port then re-map the port.
 * Use HOOK_PRIO_INIT_I2C so we re-map before charger_chips_init()
 * talks to the charger. This relies on the V1 HW not using the ISL9241 address
 * on the V0 I2C port.
 * TODO(b/155214765): Remove this check once V0 HW is no longer used.
 */
static void check_v0_charger(void)
{
	int id;

	if (i2c_read16(I2C_PORT_CHARGER_V0, ISL9241_ADDR_FLAGS,
			ISL9241_REG_MANUFACTURER_ID, &id) == EC_SUCCESS) {
		ccprints("V0 charger HW detected");
		chg_chips[0].i2c_port = I2C_PORT_CHARGER_V0;
	}
}
DECLARE_HOOK(HOOK_INIT, check_v0_charger, HOOK_PRIO_INIT_I2C);

/*****************************************************************************
 * TCPC
 */

void baseboard_tcpc_init(void)
{
	/* Enable PPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_PPC_FAULT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_PPC_INT_ODL);

	/* Enable TCPC interrupts. */
	gpio_enable_interrupt(GPIO_USB_C0_TCPC_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_TCPC_INT_ODL);

	/* Enable BC 1.2 interrupts */
	gpio_enable_interrupt(GPIO_USB_C0_BC12_INT_ODL);
	gpio_enable_interrupt(GPIO_USB_C1_BC12_INT_ODL);
}
DECLARE_HOOK(HOOK_INIT, baseboard_tcpc_init, HOOK_PRIO_INIT_I2C + 1);

/*****************************************************************************
 * IO expander
 */

struct ioexpander_config_t ioex_config[] = {
	[USBC_PORT_C0] = {
		.i2c_host_port = I2C_PORT_TCPC0,
		.i2c_slave_addr = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
	},
	[USBC_PORT_C1] = {
		.i2c_host_port = I2C_PORT_TCPC1,
		.i2c_slave_addr = NCT38XX_I2C_ADDR1_1_FLAGS,
		.drv = &nct38xx_ioexpander_drv,
	},
};
BUILD_ASSERT(ARRAY_SIZE(ioex_config) == USBC_PORT_COUNT);
BUILD_ASSERT(CONFIG_IO_EXPANDER_PORT_COUNT == USBC_PORT_COUNT);

/*****************************************************************************
 * USB-A Power
 */

const int usb_port_enable[USBA_PORT_COUNT] = {
	IOEX_EN_USB_A0_5V,
	IOEX_EN_USB_A1_5V_DB,
};

/*****************************************************************************
 * Custom Zork USB-C1 Retimer/MUX driver
 */

/*
 * PS8802 set mux board tuning.
 * Adds in board specific gain and DP lane count configuration
 */
static int board_ps8802_mux_set(const struct usb_mux *me,
				mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* Make sure the PS8802 is awake */
	rv = ps8802_i2c_wake(me);
	if (rv)
		return rv;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		rv = ps8802_i2c_field_update16(me,
					PS8802_REG_PAGE2,
					PS8802_REG2_USB_SSEQ_LEVEL,
					PS8802_USBEQ_LEVEL_UP_MASK,
					PS8802_USBEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		rv = ps8802_i2c_field_update8(me,
					PS8802_REG_PAGE2,
					PS8802_REG2_DPEQ_LEVEL,
					PS8802_DPEQ_LEVEL_UP_MASK,
					PS8802_DPEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Enable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 1);
	} else {
		/* Disable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 0);
	}

	return rv;
}

/*
 * PS8818 set mux board tuning.
 * Adds in board specific gain and DP lane count configuration
 */
static int board_ps8818_mux_set(const struct usb_mux *me,
				mux_state_t mux_state)
{
	int rv = EC_SUCCESS;

	/* USB specific config */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/* Boost the USB gain */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX1EQ_10G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX2EQ_10G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX1EQ_5G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_APTX2EQ_5G_LEVEL,
					PS8818_EQ_LEVEL_UP_MASK,
					PS8818_EQ_LEVEL_UP_19DB);
		if (rv)
			return rv;
	}

	/* DP specific config */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		/* Boost the DP gain */
		rv = ps8818_i2c_field_update8(me,
					PS8818_REG_PAGE1,
					PS8818_REG1_DPEQ_LEVEL,
					PS8818_DPEQ_LEVEL_UP_MASK,
					PS8818_DPEQ_LEVEL_UP_19DB);
		if (rv)
			return rv;

		/* Enable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 1);
	} else {
		/* Disable IN_HPD on the DB */
		ioex_set_level(IOEX_USB_C1_HPD_IN_DB, 0);
	}

	return rv;
}

const struct usb_mux usbc1_ps8802 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = PS8802_I2C_ADDR_FLAGS,
	.driver = &ps8802_usb_mux_driver,
	.board_set = &board_ps8802_mux_set,
};
const struct usb_mux usbc1_ps8818 = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_TCPC1,
	.i2c_addr_flags = PS8818_I2C_ADDR_FLAGS,
	.driver = &ps8818_usb_retimer_driver,
	.board_set = &board_ps8818_mux_set,
};
struct usb_mux usbc1_amd_fp5_usb_mux = {
	.usb_port = USBC_PORT_C1,
	.i2c_port = I2C_PORT_USB_AP_MUX,
	.i2c_addr_flags = AMD_FP5_MUX_I2C_ADDR_FLAGS,
	.driver = &amd_fp5_usb_mux_driver,
};

/*****************************************************************************
 * HDMI HPD
 */

static void hdmi_hpd_handler(void)
{
	int hpd = 0;

	/* Pass HPD through from DB OPT1 HDMI connector to AP's DP1. */
	ioex_get_level(IOEX_HDMI_CONN_HPD_3V3_DB, &hpd);
	gpio_set_level(GPIO_DP1_HPD, hpd);
	ccprints("HDMI HPD %d", hpd);
}
DECLARE_DEFERRED(hdmi_hpd_handler);

void hdmi_hpd_interrupt(enum ioex_signal signal)
{
	/* Debounce for 2 msec. */
	hook_call_deferred(&hdmi_hpd_handler_data, (2 * MSEC));
}
