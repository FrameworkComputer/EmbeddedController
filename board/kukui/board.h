/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Kukui */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#ifdef BOARD_KRANE
#define VARIANT_KUKUI_BATTERY_MM8013
#define VARIANT_KUKUI_POGO_KEYBOARD
#else
#define VARIANT_KUKUI_BATTERY_MAX17055
#endif

#define VARIANT_KUKUI_CHARGER_MT6370
#define VARIANT_KUKUI_DP_MUX_GPIO
#define VARIANT_KUKUI_TABLET_PWRBTN

#ifndef SECTION_IS_RW
#define VARIANT_KUKUI_NO_SENSORS
#endif /* SECTION_IS_RW */

#include "baseboard.h"

#define CONFIG_USB_MUX_IT5205
#define CONFIG_USB_MUX_VIRTUAL
#define CONFIG_VOLUME_BUTTONS
#define CONFIG_USB_MUX_RUNTIME_CONFIG

/* Battery */
#ifdef BOARD_KRANE
#define BATTERY_DESIRED_CHARGING_CURRENT    3500  /* mA */
#else
#define BATTERY_DESIRED_CHARGING_CURRENT    2000  /* mA */
#endif /* BOARD_KRANE */

#ifdef BOARD_KRANE
#define CONFIG_CHARGER_MT6370_BACKLIGHT
#endif /* BOARD_KRANE */

#ifdef BOARD_KUKUI
/* kukui doesn't have BC12_DET_EN pin */
#undef CONFIG_CHARGER_MT6370_BC12_GPIO
#endif

/* Motion Sensors */
#ifdef SECTION_IS_RW
#ifndef BOARD_KRANE
#define CONFIG_MAG_BMI_BMM150
#define CONFIG_ACCELGYRO_SEC_ADDR_FLAGS BMM150_ADDR0_FLAGS
#define CONFIG_MAG_CALIBRATE
#endif /* !BOARD_KRANE */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)
#define CONFIG_ALS

#define ALS_COUNT 1
#define CONFIG_ALS_TCS3400
#define CONFIG_ALS_TCS3400_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(CLEAR_ALS)
#define CONFIG_ALS_TCS3400_EMULATED_IRQ_EVENT
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(CLEAR_ALS)

/* Camera VSYNC */
#define CONFIG_SYNC
#define CONFIG_SYNC_COMMAND
#define CONFIG_SYNC_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(VSYNC)
#endif /* SECTION_IS_RW */

/* I2C ports */
#define I2C_PORT_CHARGER  0
#define I2C_PORT_TCPC0    0
#define I2C_PORT_USB_MUX  0
#define I2C_PORT_BATTERY  1
#define I2C_PORT_VIRTUAL_BATTERY I2C_PORT_BATTERY
#define I2C_PORT_ACCEL    1
#define I2C_PORT_BC12     1
#define I2C_PORT_ALS      1

/* Route sbs host requests to virtual battery driver */
#define VIRTUAL_BATTERY_ADDR_FLAGS 0x0B

/* Define the host events which are allowed to wakeup AP in S3. */
#define CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK \
		(EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN) |\
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON) |\
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_MODE_CHANGE))

/* MKBP */
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_EVENT_WAKEUP_MASK \
	(BIT(EC_MKBP_EVENT_SENSOR_FIFO) | BIT(EC_MKBP_EVENT_HOST_EVENT))

#define PD_OPERATING_POWER_MW 15000

#ifndef __ASSEMBLER__

enum adc_channel {
	/* Real ADC channels begin here */
	ADC_BOARD_ID = 0,
	ADC_EC_SKU_ID,
	ADC_BATT_ID,
	ADC_POGO_ADC_INT_L,
	ADC_CH_COUNT
};

/* power signal definitions */
enum power_signal {
	AP_IN_S3_L,
	PMIC_PWR_GOOD,

	/* Number of signals */
	POWER_SIGNAL_COUNT,
};

/* Motion sensors */
enum sensor_id {
	LID_ACCEL = 0,
	LID_GYRO,
#ifdef CONFIG_MAG_BMI_BMM150
	LID_MAG,
#endif /* CONFIG_MAG_BMI_BMM150 */
	CLEAR_ALS,
	RGB_ALS,
	VSYNC,
	SENSOR_COUNT,
};

enum charge_port {
	CHARGE_PORT_USB_C,
#if CONFIG_DEDICATED_CHARGE_PORT_COUNT > 0
	CHARGE_PORT_POGO,
#endif
};

#include "gpio_signal.h"
#include "registers.h"

#ifdef SECTION_IS_RO
/* Interrupt handler for emmc task */
void emmc_cmd_interrupt(enum gpio_signal signal);
#endif

void board_reset_pd_mcu(void);
int board_get_version(void);
int board_is_sourcing_vbus(int port);
void pogo_adc_interrupt(enum gpio_signal signal);
int board_discharge_on_ac(int enable);

/* Enable double tap detection */
#define CONFIG_GESTURE_DETECTION
#define CONFIG_GESTURE_HOST_DETECTION
#define CONFIG_GESTURE_SENSOR_DOUBLE_TAP 0
#define CONFIG_GESTURE_SENSOR_DOUBLE_TAP_FOR_HOST
#define CONFIG_GESTURE_SAMPLING_INTERVAL_MS 5
#define CONFIG_GESTURE_TAP_THRES_MG 100
#define CONFIG_GESTURE_TAP_MAX_INTERSTICE_T 500
#define CONFIG_GESTURE_DETECTION_MASK \
	BIT(CONFIG_GESTURE_SENSOR_DOUBLE_TAP)

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
