/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trembyle board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_ZORK_DALBOZ

#include <stdbool.h>
#include "baseboard.h"

#define CONFIG_USBC_PPC_NX20P3483
#define CONFIG_USB_MUX_PS8740
#define CONFIG_USB_MUX_PS8743
#define CONFIG_USB_MUX_RUNTIME_CONFIG

#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PORT_ENABLE_DYNAMIC

#undef  PD_MAX_POWER_MW
#define PD_MAX_POWER_MW		45000
#undef  PD_MAX_CURRENT_MA
#define PD_MAX_CURRENT_MA	3000
#undef  CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 40000

#define CONFIG_CHARGER_PROFILE_OVERRIDE

/* USB-A config */
#define GPIO_USB1_ILIM_SEL IOEX_USB_A0_CHARGE_EN_L
#define GPIO_USB2_ILIM_SEL IOEX_USB_A1_CHARGE_EN_DB_L

/* Power  LEDs */
#define CONFIG_LED_ONOFF_STATES_BAT_LOW 10

/* Motion sensing drivers */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCELGYRO_ICM426XX
#define CONFIG_ACCELGYRO_ICM426XX_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCEL_KX022
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_TABLET_MODE
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

/*
 * Jelboz's battery takes several seconds to come back out of its disconnect
 * state (~4 seconds on the unit I have, so give it a little more for margin).
 */
#undef CONFIG_POWER_BUTTON_INIT_TIMEOUT
#define CONFIG_POWER_BUTTON_INIT_TIMEOUT 5

/* GPIO mapping from board specific name to EC common name. */
#define CONFIG_BATTERY_PRESENT_GPIO	GPIO_EC_BATT_PRES_ODL
#define CONFIG_SCI_GPIO			GPIO_EC_FCH_SCI_ODL
#define GPIO_AC_PRESENT			GPIO_ACOK_OD
#define GPIO_CPU_PROCHOT		GPIO_PROCHOT_ODL
#define GPIO_EC_INT_L			GPIO_EC_AP_INT_ODL
#define GPIO_ENABLE_BACKLIGHT_L		GPIO_EC_EDP_BL_DISABLE
#define GPIO_ENTERING_RW		GPIO_EC_ENTERING_RW
#define GPIO_KBD_KSO2			GPIO_EC_KSO_02_INV
#define GPIO_PCH_PWRBTN_L		GPIO_EC_FCH_PWR_BTN_L
#define GPIO_PCH_RSMRST_L		GPIO_EC_FCH_RSMRST_L
#define GPIO_PCH_SLP_S3_L		GPIO_SLP_S3_L
#define GPIO_PCH_SLP_S5_L		GPIO_SLP_S5_L
#define GPIO_PCH_SYS_PWROK		GPIO_EC_FCH_PWROK
#define GPIO_PCH_WAKE_L			GPIO_EC_FCH_WAKE_L
#define GPIO_POWER_BUTTON_L		GPIO_EC_PWR_BTN_ODL
#define GPIO_S0_PGOOD			GPIO_S0_PWROK_OD
#define GPIO_S5_PGOOD			GPIO_EC_PWROK_OD
#define GPIO_SYS_RESET_L		GPIO_EC_SYS_RST_L
#define GPIO_VOLUME_DOWN_L		GPIO_VOLDN_BTN_ODL
#define GPIO_VOLUME_UP_L		GPIO_VOLUP_BTN_ODL
#define GPIO_WP_L			GPIO_EC_WP_L
#define GPIO_PACKET_MODE_EN		GPIO_EC_H1_PACKET_MODE

#ifndef __ASSEMBLER__

/* This I2C moved. Temporarily detect and support the V0 HW. */
extern int I2C_PORT_BATTERY;

enum adc_channel {
	ADC_TEMP_SENSOR_CHARGER,
	ADC_TEMP_SENSOR_SOC,
	ADC_CH_COUNT
};

enum battery_type {
	BATTERY_CM1500,
	BATTERY_TYPE_COUNT,
};

enum pwm_channel {
	PWM_CH_KBLIGHT = 0,
	PWM_CH_COUNT
};

enum ioex_port {
	IOEX_C0_NCT3807 = 0,
	IOEX_C1_NCT3807,
	IOEX_PORT_COUNT
};

#define PORT_TO_HPD(port) ((port == 0) \
	? GPIO_USB3_C0_DP2_HPD \
	: GPIO_DP1_HPD)

enum temp_sensor_id {
	TEMP_SENSOR_CHARGER = 0,
	TEMP_SENSOR_SOC,
	TEMP_SENSOR_CPU,
	TEMP_SENSOR_COUNT
};

enum usba_port {
	USBA_PORT_A0 = 0,
	USBA_PORT_A1,
	USBA_PORT_COUNT
};

enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
	USBC_PORT_COUNT
};

/*****************************************************************************
 * CBI EC FW Configuration
 */
/**
 * SHUBOZ_MB_USBAC
 *	USB-A0  Speed: 5 Gbps
 *		Retimer: none
 *	USB-C0  Speed: 5 Gbps
 *		Retimer: none
 *		TCPC: NCT3807
 *		PPC: AOZ1380
 *		IOEX: TCPC
 */
enum ec_cfg_usb_mb_type {
	SHUBOZ_MB_USBAC = 0,
};

/**
 * SHUBOZ_DB_D_OPT1_USBAC
 *	USB-A1  Speed: 5 Gbps
 *		Retimer: TUSB522
 *	USB-C1  Speed: 5 Gbps
 *		Retimer: PS8740
 *		TCPC: NCT3807
 *		PPC: NX20P3483
 *		IOEX: TCPC
 *	HDMI    Exists: no
 *		Retimer: none
 *		MST Hub: none
 */
enum ec_cfg_usb_db_type {
	SHUBOZ_DB_D_OPT1_USBAC = 0,
};

#include "cbi_ec_fw_config.h"

void board_reset_pd_mcu(void);

/* Common definition for the USB PD interrupt handlers. */
void tcpc_alert_event(enum gpio_signal signal);
void bc12_interrupt(enum gpio_signal signal);
void ppc_interrupt(enum gpio_signal signal);
void motion_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
