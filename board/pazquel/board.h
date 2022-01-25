/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trogdor board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "baseboard.h"

/* TODO(waihong): Remove the following bringup features */
#define CONFIG_BRINGUP
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands. */
#define CONFIG_USB_PD_DEBUG_LEVEL 3
#define CONFIG_CMD_GPIO_EXTENDED
#define CONFIG_CMD_POWERINDEBUG
#define CONFIG_I2C_DEBUG

/* Internal SPI flash on NPCX7 */
#define CONFIG_FLASH_SIZE_BYTES (512 * 1024)  /* 512KB internal spi flash */

/* Keyboard */
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_KEYBOARD_REFRESH_ROW3
#define CONFIG_PWM_KBLIGHT

/* Battery */
#define CONFIG_BATTERY_DEVICE_CHEMISTRY "LION"
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_LOW_VOLTAGE_PROTECTION

/* BC 1.2 Charger */
#define CONFIG_BC12_DETECT_PI3USB9201

/* USB */
#define CONFIG_USB_PD_TCPM_PS8805
#define CONFIG_USB_PD_TCPM_PS8805_FORCE_DID
#define CONFIG_USBC_PPC_SN5S330
#define CONFIG_USB_PD_PORT_MAX_COUNT 2

/* USB-A */
#define USB_PORT_COUNT 1
#define CONFIG_USB_PORT_POWER_DUMB

/* Sensors */
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT
/* BMI160 Base accel/gyro */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define OPT3001_I2C_ADDR_FLAGS OPT3001_I2C_ADDR1_FLAGS

/* BMA253 lid accel */
#define CONFIG_ACCEL_BMA255
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL
#define CONFIG_LID_ANGLE_UPDATE

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define CONFIG_GMR_TABLET_MODE

/* GPIO alias */
#define GPIO_AC_PRESENT GPIO_ACOK_OD
#define GPIO_WP_L GPIO_EC_WP_ODL
#define GPIO_SWITCHCAP_PG GPIO_SWITCHCAP_GPIO_1
#define GPIO_ACOK_OD GPIO_CHG_ACOK_OD

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_VBUS,
	ADC_AMON_BMON,
	ADC_PSYS,
	ADC_CH_COUNT
};

/* Motion sensors */
enum sensor_id {
	LID_ACCEL = 0,
	BASE_ACCEL,
	BASE_GYRO,
	SENSOR_COUNT,
};

enum pwm_channel {
	PWM_CH_KBLIGHT = 0,
	PWM_CH_DISPLIGHT,
	PWM_CH_COUNT
};

enum battery_type {
	BATTERY_GANFENG,
	BATTERY_POWTECH_SG20QT1C,
	BATTERY_TYPE_COUNT,
};
/* Reset all TCPCs. */
void board_reset_pd_mcu(void);
void board_set_tcpc_power_mode(int port, int mode);

#endif /* !defined(__ASSEMBLER__) */

#endif /* __CROS_EC_BOARD_H */
