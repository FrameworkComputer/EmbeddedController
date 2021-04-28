/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Goroh board configuration */

#include "adc.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/als_tcs3400.h"
#include "driver/charger/isl923x.h"
#include "driver/ppc/syv682x.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/temp_sensor/thermistor.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "spi.h"
#include "switch.h"
#include "tablet_mode.h"
#include "task.h"
#include "temp_sensor.h"
#include "timer.h"
#include "uart.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

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

static struct bmi_drv_data_t g_bmi160_data;
static struct stprivate_data g_lis2dwl_data;

/* Matrix to rotate accelerometer into standard reference frame */
static const mat33_fp_t base_standard_ref = {
	{0, FLOAT_TO_FP(1), 0},
	{FLOAT_TO_FP(-1), 0, 0},
	{0, 0, FLOAT_TO_FP(1)},
};

static void update_rotation_matrix(void)
{
	if (board_get_version() >= 2) {
		motion_sensors[BASE_ACCEL].rot_standard_ref =
			&base_standard_ref;
		motion_sensors[BASE_GYRO].rot_standard_ref =
			&base_standard_ref;
	}
}
DECLARE_HOOK(HOOK_INIT, update_rotation_matrix, HOOK_PRIO_INIT_ADC + 2);

struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.rot_standard_ref = NULL, /* identity matrix */
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.config = {
			/* Sensor on for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
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
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = NULL, /* identity matrix */
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
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

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	{"VBUS", ADC_MAX_MVOLT * 10, ADC_READ_MAX + 1, 0, CHIP_ADC_CH0},
	{"BOARD_ID_0", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH1},
	{"TEMP_VDD_CPU", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH2},
	{"TEMP_VDD_GPU", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH3},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* PWM */

/*
 * PWM channels. Must be in the exactly same order as in enum pwm_channel.
 * There total three 16 bits clock prescaler registers for all pwm channels,
 * so use the same frequency and prescaler register setting is required if
 * number of pwm channel greater than three.
 *
 * TODO(yllin): configure PWM
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
	[PWM_CH_FAN] = {
		.channel = 2,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4
	},
	[PWM_CH_KB_BL] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq_hz = 324, /* maximum supported frequency */
		.pcfsr_sel = PWM_PRESCALER_C4
	},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

int board_accel_force_mode_mask(void)
{
	int version = board_get_version();

	if (version == -1 || version >= 2)
		return 0;
	return BIT(LID_ACCEL);
}

static void board_suspend(void)
{
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_suspend, HOOK_PRIO_DEFAULT);

static void board_resume(void)
{
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_resume, HOOK_PRIO_DEFAULT);


