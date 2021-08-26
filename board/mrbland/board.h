/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mrbland board configuration */

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

#define CONFIG_BUTTON_TRIGGERED_RECOVERY

/* Internal SPI flash on NPCX7 */
#define CONFIG_FLASH_SIZE_BYTES (512 * 1024)  /* 512KB internal spi flash */

/* Switchcap */
#define CONFIG_LN9310

/* Battery */
#define CONFIG_BATTERY_DEVICE_CHEMISTRY  "LION"
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_VENDOR_PARAM

/* Enable PD3.0 */
#define CONFIG_USB_PD_REV30

/* BC 1.2 Charger */
#define CONFIG_BC12_DETECT_PI3USB9201

/* USB */
#define CONFIG_USB_PD_TCPM_MULTI_PS8XXX
#define CONFIG_USB_PD_TCPM_PS8755
#define CONFIG_USB_PD_TCPM_PS8805
#define CONFIG_USBC_PPC_SN5S330
#define CONFIG_USB_PD_PORT_MAX_COUNT 1

/* I2C */
#undef I2C_PORT_TCPC0
#define I2C_PORT_TCPC0   NPCX_I2C_PORT2_0

/* Lid accel/gyro */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)
#define OPT3001_I2C_ADDR_FLAGS OPT3001_I2C_ADDR1_FLAGS
#define CONFIG_ACCELGYRO_ICM42607
#define CONFIG_ACCELGYRO_ICM42607_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define CONFIG_FRONT_PROXIMITY_SWITCH

#define CONFIG_DETACHABLE_BASE
#define CONFIG_BASE_ATTACHED_SWITCH

/* GPIO alias */
#define GPIO_AC_PRESENT GPIO_CHG_ACOK_OD
#define GPIO_WP_L GPIO_EC_FLASH_WP_ODL
#define GPIO_PMIC_RESIN_L GPIO_PM845_RESIN_L
#define GPIO_SWITCHCAP_PG_INT_L GPIO_LN9310_INT

#define CONFIG_MKBP_INPUT_DEVICES

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_VBUS,
	ADC_AMON_BMON,
	ADC_PSYS,
	ADC_BASE_DET,
	ADC_CH_COUNT
};

/* Motion sensors */
enum sensor_id {
	LID_ACCEL = 0,
	LID_GYRO,
	SENSOR_COUNT,
};

enum pwm_channel {
	PWM_CH_DISPLIGHT = 0,
	PWM_CH_COUNT
};

/* List of possible batteries */
enum battery_type {
	BATTERY_GH02047XL_1C,
	BATTERY_GH02047XL,
	BATTERY_DS02032XL,
	BATTERY_DS02032XL_1C,
	BATTERY_L21D4PG0,
	BATTERY_L21M4PG0,
	BATTERY_TYPE_COUNT,
};

/* Reset all TCPCs. */
void board_reset_pd_mcu(void);
void board_set_tcpc_power_mode(int port, int mode);
/* Base detection */
void base_detect_interrupt(enum gpio_signal signal);

void motion_interrupt(enum gpio_signal signal);

#endif /* !defined(__ASSEMBLER__) */

#endif /* __CROS_EC_BOARD_H */
