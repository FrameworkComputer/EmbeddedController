/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer board-specific configuration */

#include "button.h"
#include "common.h"
#include "accelgyro.h"
#include "driver/accel_bma2x2.h"
#include "driver/accelgyro_bmi260.h"
#include "driver/als_tcs3400.h"
#include "driver/retimer/bb_retimer.h"
#include "driver/sync.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "tablet_mode.h"
#include "throttle_ap.h"
#include "uart.h"
#include "usb_pd_tbt.h"
#include "util.h"

#include "gpio_list.h" /* Must come after other header files. */

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/*
 * Reconfigure Volteer GPIOs based on the board ID
 */
__override void config_volteer_gpios(void)
{
	/* Legacy support for the first board build */
	if (get_board_id() == 0) {
		CPRINTS("Configuring GPIOs for board ID 0");

		/* Reassign USB_C1_RT_RST_ODL */
		bb_controls[USBC_PORT_C1].retimer_rst_gpio =
			GPIO_USB_C1_RT_RST_ODL_BOARDID_0;
		ps8xxx_rst_odl = GPIO_USB_C1_RT_RST_ODL_BOARDID_0;

		/* Reassign EC_VOLUP_BTN_ODL */
		button_reassign_gpio(BUTTON_VOLUME_UP,
			GPIO_EC_VOLUP_BTN_ODL_BOARDID_0);
	}
}

static void board_init(void)
{
	/* TODO */
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

__override enum tbt_compat_cable_speed board_get_max_tbt_speed(int port)
{
	/* Routing length exceeds 205mm prior to connection to re-timer */
	if (port == USBC_PORT_C1)
		return TBT_SS_U32_GEN1_GEN2;

	/*
	 * Thunderbolt-compatible mode not supported
	 *
	 * TODO (b/147726366): All the USB-C ports need to support same speed.
	 * Need to fix once USB-C feature set is known for Volteer.
	 */
	return TBT_SS_RES_0;
}

__override bool board_is_tbt_usb4_port(int port)
{
	/*
	 * On Proto-1 only Port 1 supports TBT & USB4
	 *
	 * TODO (b/147732807): All the USB-C ports need to support same
	 * features. Need to fix once USB-C feature set is known for Volteer.
	 */
	return port == USBC_PORT_C1;
}

/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */

const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = GPIO_EN_PP5000_FAN,
};

/*
 * Fan specs from datasheet:
 * Max speed 5900 rpm (+/- 7%), minimum duty cycle 30%.
 * Minimum speed not specified by RPM. Set minimum RPM to max speed (with
 * margin) x 30%.
 *    5900 x 1.07 x 0.30 = 1894, round up to 1900
 */
const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 1900,
	.rpm_start = 1900,
	.rpm_max = 5900,
};

const struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};

/******************************************************************************/
/* MFT channels. These are logically separate from pwm_channels. */
const struct mft_t mft_channels[] = {
	[MFT_CH_0] = {
		.module = NPCX_MFT_MODULE_1,
		.clk_src = TCKC_LFCLK,
		.pwm_id = PWM_CH_FAN,
	},
};
BUILD_ASSERT(ARRAY_SIZE(mft_channels) == MFT_CH_COUNT);

/******************************************************************************/
/* I2C port map configuration */
const struct i2c_port_t i2c_ports[] = {
	{
		.name = "sensor",
		.port = I2C_PORT_SENSOR,
		.kbps = 400,
		.scl = GPIO_EC_I2C0_SENSOR_SCL,
		.sda = GPIO_EC_I2C0_SENSOR_SDA,
	},
	{
		.name = "usb_c0",
		.port = I2C_PORT_USB_C0,
		.kbps = 1000,
		.scl = GPIO_EC_I2C1_USB_C0_SCL,
		.sda = GPIO_EC_I2C1_USB_C0_SDA,
	},
	{
		.name = "usb_c1",
		.port = I2C_PORT_USB_C1,
		.kbps = 1000,
		.scl = GPIO_EC_I2C2_USB_C1_SCL,
		.sda = GPIO_EC_I2C2_USB_C1_SDA,
	},
	{
		.name = "usb_1_mix",
		.port = I2C_PORT_USB_1_MIX,
		.kbps = 100,
		.scl = GPIO_EC_I2C3_USB_1_MIX_SCL,
		.sda = GPIO_EC_I2C3_USB_1_MIX_SDA,
	},
	{
		.name = "power",
		.port = I2C_PORT_POWER,
		.kbps = 100,
		.scl = GPIO_EC_I2C5_POWER_SCL,
		.sda = GPIO_EC_I2C5_POWER_SDA,
	},
	{
		.name = "eeprom",
		.port = I2C_PORT_EEPROM,
		.kbps = 400,
		.scl = GPIO_EC_I2C7_EEPROM_SCL,
		.sda = GPIO_EC_I2C7_EEPROM_SDA,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);
