/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Kukui */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_KUKUI_BATTERY_SMART
#define VARIANT_KUKUI_CHARGER_MT6370
#define VARIANT_KUKUI_EC_STM32F098
#define VARIANT_KUKUI_POGO_KEYBOARD
#define VARIANT_KUKUI_TABLET_PWRBTN
#undef CONFIG_CMD_MFALLOW

#ifndef SECTION_IS_RW
#define VARIANT_KUKUI_NO_SENSORS
#endif /* SECTION_IS_RW */

#include "baseboard.h"

#define CONFIG_DEBUG_ASSERT_BRIEF

#define CONFIG_VOLUME_BUTTONS

#define CONFIG_USB_MUX_IT5205

#define CONFIG_LED_ONOFF_STATES

#define CONFIG_BATTERY_HW_PRESENT_CUSTOM

#define CONFIG_I2C_BITBANG_CROS_EC
#define I2C_BITBANG_PORT_COUNT 1
#undef CONFIG_I2C_NACK_RETRY_COUNT
#define CONFIG_I2C_NACK_RETRY_COUNT 3
#define CONFIG_SMBUS_PEC

/* Battery */
#define BATTERY_DESIRED_CHARGING_CURRENT 2000 /* mA */

#define CONFIG_CHARGER_MT6370_BACKLIGHT
#define CONFIG_CHARGER_MAINTAIN_VBAT

/* Motion Sensors */
#ifdef SECTION_IS_RW
#define CONFIG_ACCELGYRO_LSM6DSM
#define CONFIG_ACCEL_LSM6DSM_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)

/* Camera VSYNC */
#define CONFIG_SYNC
#define CONFIG_SYNC_COMMAND
#define CONFIG_SYNC_INT_EVENT TASK_EVENT_MOTION_SENSOR_INTERRUPT(VSYNC)
#endif /* SECTION_IS_RW */

/* Disable verbose output in EC pd */
#define CONFIG_CMD_PD_SRCCAPS_REDUCED_SIZE

/* I2C ports */
#define I2C_PORT_CHARGER 0
#define I2C_PORT_TCPC0 0
#define I2C_PORT_USB_MUX 0
#define I2C_PORT_ACCEL 1
#define I2C_PORT_BATTERY board_get_battery_i2c()
#define I2C_PORT_VIRTUAL_BATTERY I2C_PORT_BATTERY

/* Define the host events which are allowed to wakeup AP in S3. */
#define CONFIG_MKBP_INPUT_DEVICES

#define CONFIG_USB_PD_OPERATING_POWER_MW 15000

/* Free up flash in RO. */
#ifdef SECTION_IS_RO
#undef CONFIG_USB_PD_HOST_CMD
#undef CONFIG_USB_PD_CONSOLE_CMD
#endif

#ifndef __ASSEMBLER__

enum adc_channel {
	/* Real ADC channels begin here */
	ADC_BOARD_ID = 0,
	ADC_EC_SKU_ID,
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

enum battery_type {
	BATTERY_SIMPLO,
	BATTERY_CELXPERT,
	BATTERY_TYPE_COUNT,
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
/* returns the i2c port number of battery */
int board_get_battery_i2c(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
