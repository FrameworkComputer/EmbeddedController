/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledoo board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_DEDEDE_EC_NPCX796FC
#include "baseboard.h"

/*
 * Keep the system unlocked in early development.
 * TODO(b/151264302): Make sure to remove this before production!
 */
#define CONFIG_SYSTEM_UNLOCKED

/* Save some flash space */
#define CONFIG_CHIP_INIT_ROM_REGION
#undef CONFIG_CONSOLE_CMDHELP
#define CONFIG_DEBUG_ASSERT_BRIEF
#define CONFIG_USB_PD_DEBUG_LEVEL 2

/* EC console commands */
#define CONFIG_CMD_CHARGER_DUMP

/* Remove default commands to free flash space */
#undef CONFIG_CMD_ACCELSPOOF
#undef CONFIG_CMD_BATTFAKE

/* Battery */
#define CONFIG_BATTERY_FUEL_GAUGE

/* Charger */
#define CONFIG_CHARGER_RAA489000
#define PD_MAX_VOLTAGE_MV 20000
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 10
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_OCPC_DEF_RBATT_MOHMS               \
	22 /* R_DS(on) 11.6mOhm + 10mOhm sns rstr \
	    */
#define CONFIG_OCPC
#undef CONFIG_CHARGER_SINGLE_CHIP
#undef CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE
#define CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE (100 * MSEC)

/*
 * GPIO for C1 interrupts, for baseboard use
 *
 * Note this line might already have its pull up disabled for HDMI DBs, but
 * it should be fine to set again before z-state.
 */
#define GPIO_USB_C1_INT_ODL GPIO_SUB_C1_INT_EN_RAILS_ODL

/* Keyboard */
#define CONFIG_PWM_KBLIGHT

/* LED */
#define CONFIG_LED_PWM
#define CONFIG_LED_PWM_COUNT 1
#undef CONFIG_LED_PWM_NEAR_FULL_COLOR
#undef CONFIG_LED_PWM_SOC_ON_COLOR
#undef CONFIG_LED_PWM_SOC_SUSPEND_COLOR
#undef CONFIG_LED_PWM_LOW_BATT_COLOR
#define CONFIG_LED_PWM_NEAR_FULL_COLOR EC_LED_COLOR_WHITE
#define CONFIG_LED_PWM_SOC_ON_COLOR EC_LED_COLOR_WHITE
#define CONFIG_LED_PWM_SOC_SUSPEND_COLOR EC_LED_COLOR_WHITE
#define CONFIG_LED_PWM_LOW_BATT_COLOR EC_LED_COLOR_AMBER

/* PWM */
#define CONFIG_PWM
#define NPCX7_PWM1_SEL 1 /* GPIO C2 is used as PWM1. */

/* USB */
#define CONFIG_BC12_DETECT_PI3USB9201
#define CONFIG_USBC_RETIMER_NB7V904M

/* USB PD */
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_TCPM_RAA489000

/* USB defines specific to external TCPCs */
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_TCPC_LOW_POWER

/* Variant references the TCPCs to determine Vbus sourcing */
#define CONFIG_USB_PD_5V_EN_CUSTOM

#undef PD_POWER_SUPPLY_TURN_ON_DELAY
#undef PD_POWER_SUPPLY_TURN_OFF_DELAY
#undef CONFIG_USBC_VCONN_SWAP_DELAY_US
/* 20% margin added for these timings */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 13080 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 16080 /* us */
#undef CONFIG_USBC_VCONN_SWAP_DELAY_US
#define CONFIG_USBC_VCONN_SWAP_DELAY_US 787 /* us */

/* I2C configuration */
#define I2C_PORT_EEPROM NPCX_I2C_PORT7_0
#define I2C_PORT_BATTERY NPCX_I2C_PORT5_0
#define I2C_PORT_SENSOR NPCX_I2C_PORT0_0
#define I2C_PORT_USB_C0 NPCX_I2C_PORT1_0
#define I2C_PORT_SUB_USB_C1 NPCX_I2C_PORT2_0
#define I2C_PORT_USB_MUX I2C_PORT_USB_C0
/* TODO(b:147440290): Need to handle multiple charger ICs */
#define I2C_PORT_CHARGER I2C_PORT_USB_C0

#define I2C_PORT_ACCEL I2C_PORT_SENSOR

#define I2C_ADDR_EEPROM_FLAGS 0x50 /* 7b address */

/*
 * I2C pin names for baseboard
 *
 * Note: these lines will be set as i2c on start-up, but this should be
 * okay since they're ODL.
 */
#define GPIO_EC_I2C_SUB_USB_C1_SCL GPIO_EC_I2C_SUB_C1_SCL_HDMI_EN_ODL
#define GPIO_EC_I2C_SUB_USB_C1_SDA GPIO_EC_I2C_SUB_C1_SDA_HDMI_HPD_ODL

/* Sensors */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

#define CONFIG_ACCEL_BMA255 /* Lid accel */
#define CONFIG_ACCELGYRO_BMI160 /* Base accel */

/* Lid operates in forced mode, base in FIFO */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)
#define CONFIG_ACCEL_FIFO
#define CONFIG_ACCEL_FIFO_SIZE 256 /* Must be a power of 2 */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define CONFIG_GMR_TABLET_MODE

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum chg_id {
	CHARGER_PRIMARY,
	CHARGER_SECONDARY,
	CHARGER_NUM,
};

enum adc_channel {
	ADC_TEMP_SENSOR_1, /* ADC0 */
	ADC_TEMP_SENSOR_2, /* ADC1 */
	ADC_SUB_ANALOG, /* ADC2 */
	ADC_VSNS_PP3300_A, /* ADC9 */
	ADC_CH_COUNT
};

enum sensor_id { LID_ACCEL, BASE_ACCEL, BASE_GYRO, SENSOR_COUNT };

enum pwm_channel {
	PWM_CH_KBLIGHT,
	PWM_CH_LED1_AMBER,
	PWM_CH_LED2_WHITE,
	PWM_CH_COUNT,
};

/* List of possible batteries */
enum battery_type {
	BATTERY_POWER_TECH,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
