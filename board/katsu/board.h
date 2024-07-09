/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Katsu */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define BQ27541_ADDR 0x55
#define VARIANT_KUKUI_BATTERY_BQ27541
#define VARIANT_KUKUI_POGO_KEYBOARD

#define VARIANT_KUKUI_CHARGER_MT6370
#define VARIANT_KUKUI_EC_STM32F098
#define VARIANT_KUKUI_TABLET_PWRBTN

#ifndef SECTION_IS_RW
#define VARIANT_KUKUI_NO_SENSORS
#endif /* SECTION_IS_RW */

#include "baseboard.h"

#define CONFIG_USB_MUX_IT5205
#define CONFIG_VOLUME_BUTTONS
#define CONFIG_USB_MUX_RUNTIME_CONFIG

/* Battery */
#define BATTERY_DESIRED_CHARGING_CURRENT 3500 /* mA */
#define BATTERY_PROTECTION_POLICY

#define CONFIG_CHARGER_MT6370_BACKLIGHT

/* Motion Sensors */
#ifdef SECTION_IS_RW
#define CONFIG_ACCELGYRO_ICM426XX
#define CONFIG_ACCELGYRO_ICM426XX_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)

/* Camera VSYNC */
#define CONFIG_SYNC
#define CONFIG_SYNC_COMMAND
#define CONFIG_SYNC_INT_EVENT TASK_EVENT_MOTION_SENSOR_INTERRUPT(VSYNC)
#endif /* SECTION_IS_RW */

/* I2C ports */
#define I2C_PORT_CHARGER 0
#define I2C_PORT_TCPC0 0
#define I2C_PORT_USB_MUX 0
#define I2C_PORT_BATTERY 1
#define I2C_PORT_VIRTUAL_BATTERY I2C_PORT_BATTERY
#define I2C_PORT_ACCEL 1
#define I2C_PORT_BC12 1

/* Route sbs host requests to virtual battery driver */
#define VIRTUAL_BATTERY_ADDR_FLAGS 0x0B

/* MKBP */
#define CONFIG_MKBP_INPUT_DEVICES
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_EVENT_WAKEUP_MASK \
	(BIT(EC_MKBP_EVENT_SENSOR_FIFO) | BIT(EC_MKBP_EVENT_HOST_EVENT))
#undef CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK
#define CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK                   \
	(EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN) |        \
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON) |    \
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_CONNECTED) |    \
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_DISCONNECTED) | \
	 EC_HOST_EVENT_MASK(EC_HOST_EVENT_MODE_CHANGE))

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
	VSYNC,
	SENSOR_COUNT,
};

enum charge_port {
	CHARGE_PORT_USB_C,
};

#include "gpio_signal.h"
#include "registers.h"

#ifdef SECTION_IS_RO
/* Interrupt handler for emmc task */
void emmc_cmd_interrupt(enum gpio_signal signal);
#endif

void board_reset_pd_mcu(void);
int board_get_version(void);
void pogo_adc_interrupt(enum gpio_signal signal);
int board_discharge_on_ac(int enable);

/* Enable double tap detection */
#define CONFIG_GESTURE_DETECTION
#define CONFIG_GESTURE_SENSOR_DOUBLE_TAP
#define CONFIG_GESTURE_TAP_SENSOR 0
#define CONFIG_GESTURE_SENSOR_DOUBLE_TAP_FOR_HOST
#define CONFIG_GESTURE_SAMPLING_INTERVAL_MS 5
#define CONFIG_GESTURE_TAP_THRES_MG 100
#define CONFIG_GESTURE_TAP_MAX_INTERSTICE_T 500
#define CONFIG_GESTURE_DETECTION_MASK BIT(CONFIG_GESTURE_TAP_SENSOR)

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
