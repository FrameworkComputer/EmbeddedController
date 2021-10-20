/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Corsola board configuration */

#include "adc.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/accelgyro_icm426xx.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "regulator.h"
#include "spi.h"
#include "switch.h"
#include "tablet_mode.h"
#include "task.h"
#include "timer.h"
#include "uart.h"

/* Initialize board. */
static void board_init(void)
{
	/* Enable motion sensor interrupt */
	gpio_enable_interrupt(GPIO_BASE_IMU_INT_L);
	gpio_enable_interrupt(GPIO_LID_ACCEL_INT_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* Sensor */
static struct mutex g_base_mutex;
static struct mutex g_lid_mutex;

static struct stprivate_data g_lis2dwl_data;
static struct icm_drv_data_t g_icm426xx_data;

struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: icm426xx: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_ICM426XX,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &icm426xx_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_icm426xx_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
		.default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs. */
		.rot_standard_ref = NULL,
		.min_frequency = ICM426XX_ACCEL_MIN_FREQ,
		.max_frequency = ICM426XX_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_ICM426XX,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &icm426xx_drv,
		.mutex = &g_base_mutex,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = NULL,
		.min_frequency = ICM426XX_GYRO_MIN_FREQ,
		.max_frequency = ICM426XX_GYRO_MAX_FREQ,
	},
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LIS2DWL,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &lis2dw12_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_lis2dwl_data,
		.int_signal = GPIO_LID_ACCEL_INT_L,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = LIS2DWL_ADDR1_FLAGS,
		.flags = MOTIONSENSE_FLAG_INT_SIGNAL,
		.rot_standard_ref = NULL, /* identity matrix */
		.default_range = 2, /* g */
		.min_frequency = LIS2DW12_ODR_MIN_VAL,
		.max_frequency = LIS2DW12_ODR_MAX_VAL,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 12500 | ROUND_UP_FLAG,
			},
			/* Sensor on for lid angle detection */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

void motion_interrupt(enum gpio_signal signal)
{
	icm426xx_interrupt(signal);
}

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	/* Convert to mV (3000mV/1024). */
	{"VBUS_C0", ADC_MAX_MVOLT * 10, ADC_READ_MAX + 1, 0, CHIP_ADC_CH0},
	{"BOARD_ID_0", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH1},
	{"BOARD_ID_1", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH2},
	/* AMON/BMON gain = 17.97 */
	{"CHARGER_AMON_R", ADC_MAX_MVOLT * 1000 / 17.97, ADC_READ_MAX + 1, 0,
	 CHIP_ADC_CH3},
	{"VBUS_C1", ADC_MAX_MVOLT * 10, ADC_READ_MAX + 1, 0, CHIP_ADC_CH5},
	{"CHARGER_PMON", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH6},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* PWM */

/*
 * PWM channels. Must be in the exactly same order as in enum pwm_channel.
 * There total three 16 bits clock prescaler registers for all pwm channels,
 * so use the same frequency and prescaler register setting is required if
 * number of pwm channel greater than three.
 */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_LED1] = {
		.channel = 0,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4
	},
	[PWM_CH_LED2] = {
		.channel = 1,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4
	},
	[PWM_CH_LED3] = {
		.channel = 2,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

static void board_suspend(void)
{
	gpio_set_level(GPIO_EN_5V_USM, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_suspend, HOOK_PRIO_DEFAULT);

static void board_resume(void)
{
	gpio_set_level(GPIO_EN_5V_USM, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_resume, HOOK_PRIO_DEFAULT);
