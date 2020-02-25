/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trembyle board configuration */

#include "button.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "extpower.h"
#include "fan.h"
#include "fan_chip.h"
#include "gpio.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "usb_charge.h"

#include "gpio_list.h"

#ifdef HAS_TASK_MOTIONSENSE

/* Motion sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

/* sensor private data */
static struct stprivate_data g_lis2dwl_data;
static struct lsm6dsm_data g_lsm6dsm_data = LSM6DSM_DATA;

/* TODO(gcc >= 5.0) Remove the casts to const pointer at rot_standard_ref */
struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
	 .name = "Lid Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_LIS2DWL,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &lis2dw12_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_lis2dwl_data,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = LIS2DWL_ADDR1_FLAGS,
	 .rot_standard_ref = NULL,
	 .default_range = 2, /* g, enough for laptop. */
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

	[BASE_ACCEL] = {
	 .name = "Base Accel",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_LSM6DSM,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &lsm6dsm_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = LSM6DSM_ST_DATA(g_lsm6dsm_data,
			MOTIONSENSE_TYPE_ACCEL),
	 .int_signal = GPIO_6AXIS_INT_L,
	 .flags = MOTIONSENSE_FLAG_INT_SIGNAL,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	 .default_range = 4, /* g, enough for laptop */
	 .rot_standard_ref = NULL,
	 .min_frequency = LSM6DSM_ODR_MIN_VAL,
	 .max_frequency = LSM6DSM_ODR_MAX_VAL,
	 .config = {
		 /* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 13000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
		/* Sensor on for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
	 },
	},

	[BASE_GYRO] = {
	 .name = "Base Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_LSM6DSM,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &lsm6dsm_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = LSM6DSM_ST_DATA(g_lsm6dsm_data,
			MOTIONSENSE_TYPE_GYRO),
	.int_signal = GPIO_6AXIS_INT_L,
	.flags = MOTIONSENSE_FLAG_INT_SIGNAL,
	 .port = I2C_PORT_SENSOR,
	 .i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	 .default_range = 1000 | ROUND_UP_FLAG, /* dps */
	 .rot_standard_ref = NULL,
	 .min_frequency = LSM6DSM_ODR_MIN_VAL,
	 .max_frequency = LSM6DSM_ODR_MAX_VAL,
	},
};

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

#endif /* HAS_TASK_MOTIONSENSE */

/* These GPIOs moved. Temporarily detect and support the V0 HW. */
enum gpio_signal GPIO_PCH_PWRBTN_L = GPIO_EC_FCH_PWR_BTN_L;
enum gpio_signal GPIO_PCH_SYS_PWROK = GPIO_EC_FCH_PWROK;

void board_update_sensor_config_from_sku(void)
{
	int data;

	/*
	 * If the CBI EEPROM is found on the battery I2C port then we are
	 * running on V0 HW so re-map the GPIOs that moved.
	 */
	if ((system_get_sku_id() == 0x7fffffff)
	    && (i2c_read8(I2C_PORT_BATTERY, I2C_ADDR_EEPROM_FLAGS, 0, &data)
		== EC_SUCCESS)) {
		ccprints("V0 HW detected");
		GPIO_PCH_PWRBTN_L = GPIO_EC_FCH_PWR_BTN_L_V0;
		GPIO_PCH_SYS_PWROK = GPIO_EC_FCH_PWROK_V0;
	}

	/* Enable Gyro interrupts */
	gpio_enable_interrupt(GPIO_6AXIS_INT_L);
}

const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 100,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);
