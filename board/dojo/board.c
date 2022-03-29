/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Dojo board configuration */

#include "common.h"
#include "console.h"
#include "cros_board_info.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_icm42607.h"
#include "driver/accelgyro_icm_common.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "motion_sense.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "system.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

uint32_t board_version;

/* Keyboard scan setting */
__override struct keyboard_scan_config keyscan_config = {
	/* Increase from 50 us, because KSO_02 passes through the H1. */
	.output_settle_us = 80,
	.debounce_down_us = 9 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 3 * MSEC,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = 100 * MSEC,
	.actual_key_mask = {
		0x1c, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xa4, 0xff, 0xfe, 0x55, 0xfa, 0xca  /* full set */
	},
};

/* Temperature charging table */
const struct temp_chg_struct temp_chg_table[] = {
	[LEVEL_0] = {
		.lo_thre = 0,
		.hi_thre = 68,
		.chg_curr = 3000,
	},
	[LEVEL_1] = {
		.lo_thre = 63,
		.hi_thre = 74,
		.chg_curr = 1500,
	},
	[LEVEL_2] = {
		.lo_thre = 69,
		.hi_thre = 100,
		.chg_curr = 500,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_chg_table) == CHG_LEVEL_COUNT);

/* Sensor */
static struct mutex g_base_mutex;
static struct mutex g_lid_mutex;

static struct icm_drv_data_t g_icm42607_data;
static struct kionix_accel_data g_kx022_data;

/* Matrix to rotate accelrator into standard reference frame */
static const mat33_fp_t base_standard_ref = {
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

static const mat33_fp_t lid_standard_ref = {
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0, FLOAT_TO_FP(-1)}
};

struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_ICM42607,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &icm42607_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_icm42607_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = ICM42607_ADDR0_FLAGS,
		.default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs.*/
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = ICM42607_ACCEL_MIN_FREQ,
		.max_frequency = ICM42607_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_ICM42607,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &icm42607_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_icm42607_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = ICM42607_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = ICM42607_GYRO_MIN_FREQ,
		.max_frequency = ICM42607_GYRO_MAX_FREQ,
	},
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_KX022,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &kionix_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_kx022_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
		.rot_standard_ref = &lid_standard_ref,
		.default_range = 2, /* g, enough for laptop. */
		.min_frequency = KX022_ACCEL_MIN_FREQ,
		.max_frequency = KX022_ACCEL_MAX_FREQ,
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
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* PWM */

/*
 * PWM channels. Must be in the exactly same order as in enum pwm_channel.
 * There total three 16 bits clock prescaler registers for all pwm channels,
 * so use the same frequency and prescaler register setting is required if
 * number of pwm channel greater than three.
 */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_LED_C1_WHITE] = {
		.channel = 0,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
	[PWM_CH_LED_C1_AMBER] = {
		.channel = 1,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
	[PWM_CH_LED_PWR] = {
		.channel = 2,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP,
		.freq_hz = 10000, /* SYV226 supports 10~100kHz */
		.pcfsr_sel = PWM_PRESCALER_C6,
	},
	[PWM_CH_LED_C0_WHITE] = {
		.channel = 6,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
	[PWM_CH_LED_C0_AMBER] = {
		.channel = 7,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Initialize board. */
static void board_init(void)
{
	/* Enable motion sensor interrupt */
	gpio_enable_interrupt(GPIO_BASE_IMU_INT_L);
	gpio_enable_interrupt(GPIO_LID_ACCEL_INT_L);

	/* Store board version for use of something */
	cbi_get_board_version(&board_version);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_do_chipset_resume(void)
{
	gpio_set_level(GPIO_EN_PP3300_SSD, 1);
	gpio_set_level(GPIO_EN_KB_BL, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_do_chipset_resume, HOOK_PRIO_DEFAULT);

static void board_do_chipset_suspend(void)
{
	gpio_set_level(GPIO_EN_PP3300_SSD, 0);
	gpio_set_level(GPIO_EN_KB_BL, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_do_chipset_suspend, HOOK_PRIO_DEFAULT);
