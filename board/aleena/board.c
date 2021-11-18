/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Aleena board-specific configuration */

#include "button.h"
#include "driver/accelgyro_bmi_common.h"
#include "console.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/led/lm3630a.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "tablet_mode.h"
#include "task.h"

#include "gpio_list.h"

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

/* I2C port map. */
const struct i2c_port_t i2c_ports[] = {
	{
		.name = "power",
		.port = I2C_PORT_POWER,
		.kbps = 100,
		.scl  = GPIO_I2C0_SCL,
		.sda  = GPIO_I2C0_SDA
	},
	{
		.name = "tcpc0",
		.port = I2C_PORT_TCPC0,
		.kbps = 400,
		.scl  = GPIO_I2C1_SCL,
		.sda  = GPIO_I2C1_SDA
	},
	{
		.name = "tcpc1",
		.port = I2C_PORT_TCPC1,
		.kbps = 400,
		.scl  = GPIO_I2C2_SCL,
		.sda  = GPIO_I2C2_SDA
	},
	{
		.name = "thermal",
		.port = I2C_PORT_THERMAL_AP,
		.kbps = 400,
		.scl  = GPIO_I2C3_SCL,
		.sda  = GPIO_I2C3_SDA
	},
	{
		.name = "kblight",
		.port = I2C_PORT_KBLIGHT,
		.kbps = 100,
		.scl  = GPIO_I2C5_SCL,
		.sda  = GPIO_I2C5_SDA
	},
	{
		.name = "sensor",
		.port = I2C_PORT_SENSOR,
		.kbps = 400,
		.scl  = GPIO_I2C7_SCL,
		.sda  = GPIO_I2C7_SDA
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = {
		.channel = 5,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Motion sensors */
static struct mutex icm426xx_mutex;

static struct icm_drv_data_t g_icm426xx_data;

enum base_accelgyro_type {
	BASE_GYRO_NONE = 0,
	BASE_GYRO_BMI160 = 1,
	BASE_GYRO_ICM426XX = 2,
};

const mat33_fp_t base_standard_ref_icm426xx = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0,  0, FLOAT_TO_FP(1)}
};

struct motion_sensor_t icm426xx_base_accel = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_ICM426XX,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &icm426xx_drv,
	 .mutex = &icm426xx_mutex,
	 .drv_data = &g_icm426xx_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	 .default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs.*/
	 .rot_standard_ref = &base_standard_ref_icm426xx,
	 .min_frequency = ICM426XX_ACCEL_MIN_FREQ,
	 .max_frequency = ICM426XX_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100,
		 },
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		 },
	 },
};

struct motion_sensor_t icm426xx_base_gyro = {
	 .name = "Base Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_ICM426XX,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &icm426xx_drv,
	 .mutex = &icm426xx_mutex,
	 .drv_data = &g_icm426xx_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
	 .default_range = 1000, /* dps */
	 .rot_standard_ref = &base_standard_ref_icm426xx,
	 .min_frequency = ICM426XX_GYRO_MIN_FREQ,
	 .max_frequency = ICM426XX_GYRO_MAX_FREQ,
};

static enum base_accelgyro_type base_accelgyro_config;

void motion_interrupt(enum gpio_signal signal)
{
	switch (base_accelgyro_config) {
	case BASE_GYRO_ICM426XX:
		icm426xx_interrupt(signal);
		break;
	case BASE_GYRO_BMI160:
	default:
		bmi160_interrupt(signal);
		break;
	}
}

static void board_detect_motionsensor(void)
{
	int ret;
	int val;

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return;
	if (base_accelgyro_config != BASE_GYRO_NONE)
		return;

	if (board_is_convertible()) {
		/* Check base accelgyro chip */
		ret = icm_read8(&icm426xx_base_accel,
				 ICM426XX_REG_WHO_AM_I, &val);
		if (ret)
			ccprints("Get ICM fail.");
		if (val == ICM426XX_CHIP_ICM40608) {
			motion_sensors[BASE_ACCEL] = icm426xx_base_accel;
			motion_sensors[BASE_GYRO] = icm426xx_base_gyro;
		}
		base_accelgyro_config = (val == ICM426XX_CHIP_ICM40608)
			 ? BASE_GYRO_ICM426XX : BASE_GYRO_BMI160;
		ccprints("Base Accelgyro: %s", (val == ICM426XX_CHIP_ICM40608)
			 ? "ICM40608" : "BMI160");
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_detect_motionsensor,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, board_detect_motionsensor, HOOK_PRIO_INIT_ADC + 2);

void board_update_sensor_config_from_sku(void)
{
	if (board_is_convertible()) {
		/* Enable Gyro interrupts */
		gpio_enable_interrupt(GPIO_6AXIS_INT_L);
	} else {
		motion_sensor_count = 0;
		/* Device is clamshell only */
		tablet_set_mode(0, TABLET_TRIGGER_LID);
		/* Gyro is not present, don't allow line to float */
		gpio_set_flags(GPIO_6AXIS_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
	}
}

static void board_kblight_init(void)
{
	/*
	 * Enable keyboard backlight. This needs to be done here because
	 * the chip doesn't have power until PP3300_S0 comes up.
	 */
	gpio_set_level(GPIO_KB_BL_EN, 1);
	lm3630a_poweron();
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_kblight_init, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_KEYBOARD_FACTORY_TEST
/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 30 pins total, and there is no pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
		{-1, -1}, {0, 5}, {1, 1}, {1, 0}, {0, 6},
		{0, 7}, {-1, -1}, {-1, -1}, {1, 4}, {1, 3},
		{-1, -1}, {1, 6}, {1, 7}, {3, 1}, {2, 0},
		{1, 5}, {2, 6}, {2, 7}, {2, 1}, {2, 4},
		{2, 5}, {1, 2}, {2, 3}, {2, 2}, {3, 0},
		{-1, -1}, {0, 4}, {-1, -1}, {8, 2}, {-1, -1},
		{-1, -1},
};

const int keyboard_factory_scan_pins_used =
			ARRAY_SIZE(keyboard_factory_scan_pins);
#endif
