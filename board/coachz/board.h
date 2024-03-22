/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Coachz board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "baseboard.h"

/* On-body detection */
#define CONFIG_BODY_DETECTION
#define CONFIG_BODY_DETECTION_SENSOR LID_ACCEL
#define CONFIG_BODY_DETECTION_VAR_NOISE_FACTOR 150 /* % */
#define CONFIG_GESTURE_DETECTION
#define CONFIG_GESTURE_DETECTION_MASK BIT(CONFIG_BODY_DETECTION_SENSOR)
#define CONFIG_GESTURE_HOST_DETECTION

#define CONFIG_BUTTON_TRIGGERED_RECOVERY

/* Internal SPI flash on NPCX7 */
#define CONFIG_FLASH_SIZE_BYTES (512 * 1024) /* 512KB internal spi flash */

/* Save some flash space */
#define CONFIG_LTO
#define CONFIG_USB_PD_DEBUG_LEVEL 2
#undef CONFIG_CMD_FLASHINFO
#undef CONFIG_CMD_MMAPINFO
#undef CONFIG_CMD_ACCELSPOOF
#undef CONFIG_CMD_ACCEL_FIFO
#undef CONFIG_CMD_ACCEL_INFO
#undef CONFIG_CMD_TASK_RESET
#undef CONFIG_CONSOLE_CMDHELP

/* Battery */
#define CONFIG_BATTERY_DEVICE_CHEMISTRY "LION"
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_VENDOR_PARAM

#undef CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT
#define CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT 5

/* BC 1.2 Charger */
#define CONFIG_BC12_DETECT_PI3USB9201

/* USB */
#define CONFIG_USB_PD_TCPM_MULTI_PS8XXX
#define CONFIG_USB_PD_TCPM_PS8755
#define CONFIG_USB_PD_TCPM_PS8805
#define CONFIG_USB_PD_TCPM_PS8805_FORCE_DID
#define CONFIG_USBC_PPC_SN5S330
#define CONFIG_USB_PD_PORT_MAX_COUNT 2

/* BMI160 Lid accel/gyro */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)
#define OPT3001_I2C_ADDR_FLAGS OPT3001_I2C_ADDR1_FLAGS
#define CONFIG_ACCELGYRO_BMI260
#define CONFIG_ACCELGYRO_BMI260_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define CONFIG_GMR_TABLET_MODE
#define CONFIG_FRONT_PROXIMITY_SWITCH

#define CONFIG_MKBP_INPUT_DEVICES

#define CONFIG_DETACHABLE_BASE
#define CONFIG_BASE_ATTACHED_SWITCH

/* GPIO alias */
#define GPIO_AC_PRESENT GPIO_CHG_ACOK_OD
#define GPIO_WP_L GPIO_EC_FLASH_WP_ODL
#define GPIO_PMIC_RESIN_L GPIO_PM845_RESIN_L
#define GPIO_TABLET_MODE_L GPIO_LID_360_L
#define GPIO_KS_ATTACHED_L GPIO_LID_INT_N_HALL1
#define GPIO_KS_OPEN GPIO_LID_INT_N_HALL2

/* WLC pins */
#define CONFIG_PERIPHERAL_CHARGER
#define CONFIG_DEVICE_EVENT
#define CONFIG_CTN730

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

enum pwm_channel { PWM_CH_DISPLIGHT = 0, PWM_CH_COUNT };

/* List of possible batteries */
enum battery_type {
	BATTERY_GH02047XL_1C,
	BATTERY_GH02047XL,
	BATTERY_DS02032XL,
	BATTERY_DS02032XL_1C,
	BATTERY_TYPE_COUNT,
};

/* Reset all TCPCs. */
void board_reset_pd_mcu(void);
void board_set_tcpc_power_mode(int port, int mode);
/* Base detection */
void base_detect_interrupt(enum gpio_signal signal);
/* motion sensor interrupt */
void motion_interrupt(enum gpio_signal signal);

#endif /* !defined(__ASSEMBLER__) */

#endif /* __CROS_EC_BOARD_H */
