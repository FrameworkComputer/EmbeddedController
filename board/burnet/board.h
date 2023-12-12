/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Burnet */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_KUKUI_JACUZZI
#define VARIANT_KUKUI_BATTERY_SMART
#define VARIANT_KUKUI_CHARGER_ISL9238
#define VARIANT_KUKUI_EC_STM32F098

#ifndef SECTION_IS_RW
#define VARIANT_KUKUI_NO_SENSORS
#endif /* SECTION_IS_RW */

#include "baseboard.h"

/* Free up flash space. */
#undef CONFIG_CMD_KEYBOARD

#undef CONFIG_CHIPSET_POWER_SEQ_VERSION
#define CONFIG_CHIPSET_POWER_SEQ_VERSION 1

#define CONFIG_BATTERY_HW_PRESENT_CUSTOM
#define CONFIG_BATTERY_VENDOR_PARAM
#define CONFIG_BATTERY_V2
#define CONFIG_BATTERY_COUNT 1

#define CONFIG_CHARGER_PSYS
#undef CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT
#define CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT 5

#define CONFIG_BC12_DETECT_PI3USB9201

#define CONFIG_EXTPOWER_GPIO
#undef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 200

#define CONFIG_I2C_BITBANG
#define I2C_BITBANG_PORT_COUNT 1
#undef CONFIG_I2C_NACK_RETRY_COUNT
#define CONFIG_I2C_NACK_RETRY_COUNT 10
#define CONFIG_SMBUS_PEC

#define CONFIG_USB_PD_TCPM_FUSB302
#define CONFIG_USB_PD_DISCHARGE_GPIO
#define CONFIG_USB_PD_TCPC_LOW_POWER

#define CONFIG_USB_MUX_IT5205

/* Disable verbose output in EC pd */
#define CONFIG_CMD_PD_SRCCAPS_REDUCED_SIZE

/* Motion Sensors */
#ifndef VARIANT_KUKUI_NO_SENSORS
#define CONFIG_ACCEL_BMA255 /* Lid accel */
#define CONFIG_ACCEL_KX022
#define CONFIG_ACCELGYRO_BMI160 /* Base accel */
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
/* ICM42607 Base accel/gyro */
#define CONFIG_ACCELGYRO_ICM42607
#define CONFIG_ACCELGYRO_ICM42607_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ALS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

#endif /* VARIANT_KUKUI_NO_SENSORS */

/* I2C ports */
#define I2C_PORT_BC12 0
#define I2C_PORT_TCPC0 0
#define I2C_PORT_USB_MUX 0
#define I2C_PORT_BATTERY 2
#define I2C_PORT_CHARGER 1
#define I2C_PORT_SENSORS 1
#define I2C_PORT_KB_DISCRETE 1
#define I2C_PORT_VIRTUAL_BATTERY I2C_PORT_BATTERY

/* IT8801 I2C address */
#define KB_DISCRETE_I2C_ADDR_FLAGS IT8801_I2C_ADDR1

/* Enable Accel over SPI */
#define CONFIG_SPI_ACCEL_PORT 0 /* The first SPI controller port (SPI2) */

#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO

#ifndef __ASSEMBLER__

enum adc_channel {
	/* Real ADC channels begin here */
	ADC_BOARD_ID = 0,
	ADC_EC_SKU_ID,
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
	BASE_ACCEL,
	BASE_GYRO,
	SENSOR_COUNT,
};

enum charge_port {
	CHARGE_PORT_USB_C,
};

enum battery_type {
	BATTERY_DYNAPACK_ATL,
	BATTERY_DYNAPACK_COS,
	BATTERY_SIMPLO_COS,
	BATTERY_SIMPLO_HIGHPOWER,
	BATTERY_SAMSUNG_SDI,
	BATTERY_COS,
	BATTERY_DYNAPACK_COS_44WH,
	BATTERY_DYNAPACK_ATL_44WH,
	BATTERY_SIMPLO_HIGHPOWER_44WH,
	BATTERY_TYPE_COUNT,
};

#include "gpio_signal.h"
#include "registers.h"

#ifdef SECTION_IS_RO
/* Interrupt handler for emmc task */
void emmc_cmd_interrupt(enum gpio_signal signal);
#endif

void bc12_interrupt(enum gpio_signal signal);
void board_reset_pd_mcu(void);
int board_get_version(void);

/* returns the i2c port number of charger */
int board_get_charger_i2c(void);

int board_is_convertible(void);

/* Motion sensor interrupt */
void sensor_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
