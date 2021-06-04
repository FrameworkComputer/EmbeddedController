/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chronicler board-specific configuration */
#include "bb_retimer.h"
#include "button.h"
#include "common.h"
#include "accelgyro.h"
#include "cbi_ec_fw_config.h"
#include "driver/retimer/bb_retimer.h"
#include "driver/sync.h"
#include "driver/tcpm/ps8xxx.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_8042.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "throttle_ap.h"
#include "uart.h"
#include "usb_pd.h"
#include "usb_pd_tbt.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#include "gpio_list.h" /* Must come after other header files. */

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)

/******************************************************************************/
/* Physical fans. These are logically separate from pwm_channels. */

const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = MFT_CH_0,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = GPIO_EN_PP5000_FAN,
};

const struct fan_rpm fan_rpm_0 = {
	.rpm_min   = 3400,
	.rpm_start = 3400,
	.rpm_max   = 5700,
};

const struct fan_t fans[FAN_CH_COUNT] = {
	[FAN_CH_0] = {
		.conf = &fan_conf_0,
		.rpm = &fan_rpm_0,
	},
};

/******************************************************************************/
/* EC thermal management configuration */

/*
 * Tiger Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to DDR, so we need to use the lower
 * DDR temperature limit (80 C)
 */
const static struct ec_thermal_config thermal_config_without_fan = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(77),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
};

const static struct ec_thermal_config thermal_config_with_fan = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(77),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
	.temp_fan_off = C_TO_K(41),
	.temp_fan_max = C_TO_K(59),
};

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_CHARGER]			= thermal_config_with_fan,
	[TEMP_SENSOR_2_PP3300_REGULATOR]	= thermal_config_without_fan,
	[TEMP_SENSOR_3_DDR_SOC]			= thermal_config_without_fan,
	[TEMP_SENSOR_4_FAN]			= thermal_config_without_fan,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

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
		.scl = GPIO_EC_I2C5_BATTERY_SCL,
		.sda = GPIO_EC_I2C5_BATTERY_SDA,
	},
	{
		.name = "eeprom",
		.port = I2C_PORT_EEPROM,
		.kbps = 400,
		.scl = GPIO_EC_I2C7_EEPROM_PWR_SCL_R,
		.sda = GPIO_EC_I2C7_EEPROM_PWR_SDA_R,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/******************************************************************************/
/* PWM configuration */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_FAN] = {
		.channel = 5,
		.flags = PWM_CONFIG_OPEN_DRAIN,
		.freq = 25000
	},
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = 0,
		/*
		 * Set PWM frequency to multiple of 50 Hz and 60 Hz to prevent
		 * flicker. Higher frequencies consume similar average power to
		 * lower PWM frequencies, but higher frequencies record a much
		 * lower maximum power.
		 */
		.freq = 2400,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/******************************************************************************/
/* keyboard config */
static const struct ec_response_keybd_config main_kb = {
	.num_top_row_keys = 10,
	.action_keys = {
		TK_BACK,		/* T1 */
		/*
		 *  Chronicler keyboard swaps T2 and T3 in the keyboard
		 *  matrix,So swap the actions key lookup to match.
		 *  The physical keyboard still orders the top row as
		 *  Back, Refresh, Fullscreen, etc.
		 */
		TK_FULLSCREEN,		/* T3 */
		TK_REFRESH,		/* T2 */
		TK_OVERVIEW,		/* T4 */
		TK_SNAPSHOT,		/* T5 */
		TK_BRIGHTNESS_DOWN,	/* T6 */
		TK_BRIGHTNESS_UP,	/* T7 */
		TK_VOL_MUTE,		/* T8 */
		TK_VOL_DOWN,		/* T9 */
		TK_VOL_UP,		/* T10 */
	},
	.capabilities = KEYBD_CAP_SCRNLOCK_KEY,
};

__override const struct ec_response_keybd_config
*board_vivaldi_keybd_config(void)
{
	return &main_kb;
}

/******************************************************************************/
/* keyboard factory test */
#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 24 pins total, and there is no pin 0.
 */

const int keyboard_factory_scan_pins[][2] = {
		{-1, -1}, {0, 5}, {1, 1}, {1, 0}, {0, 6},
		{0, 7}, {1, 4}, {1, 3}, {1, 6}, {1, 7},
		{3, 1}, {2, 0}, {1, 5}, {2, 6}, {2, 7},
		{2, 1}, {2, 4}, {2, 5}, {1, 2}, {2, 3},
		{2, 2}, {3, 0}, {-1, -1}, {-1, -1}, {-1, -1},
};

const int keyboard_factory_scan_pins_used =
			ARRAY_SIZE(keyboard_factory_scan_pins);
#endif
