/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Coachz board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "baseboard.h"

/* TODO(waihong): Remove the following bringup features */
#define CONFIG_BRINGUP
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands. */
#define CONFIG_USB_PD_DEBUG_LEVEL 3
#define CONFIG_CMD_AP_RESET_LOG
#define CONFIG_CMD_GPIO_EXTENDED
#define CONFIG_CMD_POWERINDEBUG
#define CONFIG_I2C_DEBUG

/* Internal SPI flash on NPCX7 */
#define CONFIG_FLASH_SIZE (512 * 1024)  /* 512KB internal spi flash */

/* Battery */
#define CONFIG_BATTERY_DEVICE_CHEMISTRY  "LION"
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_FUEL_GAUGE

/* BC 1.2 Charger */
#define CONFIG_BC12_DETECT_PI3USB9201

/* USB */
#define CONFIG_USB_PD_TCPM_PS8805
#define CONFIG_USBC_PPC_SN5S330
#define CONFIG_USB_PD_PORT_MAX_COUNT 2

/* BMI160 Lid accel/gyro */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)
#define OPT3001_I2C_ADDR_FLAGS OPT3001_I2C_ADDR1_FLAGS

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH

/* GPIO alias */
#define GPIO_AC_PRESENT GPIO_CHG_ACOK_OD
#define GPIO_WP_L GPIO_EC_FLASH_WP_ODL
#define GPIO_PMIC_RESIN_L GPIO_PM845_RESIN_L

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
	BATTERY_DS02032XL,
	BATTERY_TYPE_COUNT,
};

/* Swithcap functions */
void board_set_switchcap_power(int enable);
int board_is_switchcap_enabled(void);
int board_is_switchcap_power_good(void);
/* Custom function to indicate if sourcing VBUS */
int board_is_sourcing_vbus(int port);
/* Enable VBUS sink for a given port */
int board_vbus_sink_enable(int port, int enable);
/* Reset all TCPCs. */
void board_reset_pd_mcu(void);
void board_set_tcpc_power_mode(int port, int mode);
/* Base detection */
void base_detect_interrupt(enum gpio_signal signal);

#endif /* !defined(__ASSEMBLER__) */

#endif /* __CROS_EC_BOARD_H */
