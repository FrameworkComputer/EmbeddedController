/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trembyle board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_ZORK_DALBOZ

#include "baseboard.h"

#include <stdbool.h>

#undef CONFIG_HIBERNATE_PSL

#define CONFIG_USB_PD_PORT_MAX_COUNT 1

/* USB-A config */
#define GPIO_USB1_ILIM_SEL GPIO_USB_A0_CHARGE_EN_L
#define GPIO_USB2_ILIM_SEL GPIO_USB_A1_CHARGE_EN_DB_L

#define CONFIG_CHARGER_PROFILE_OVERRIDE
#undef CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT
#define CONFIG_CHARGER_INPUT_CURRENT_DERATE_PCT 5

/* Motion sensing drivers */
#define CONFIG_ACCELGYRO_LSM6DSM
#define CONFIG_ACCEL_LSM6DSM_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCEL_LIS2DWL
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_TABLET_MODE
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

/*
 * Vilboz's battery takes ~3 seconds to come back out of its disconnect state,
 * so give it a little more for margin.
 */
#undef CONFIG_POWER_BUTTON_INIT_TIMEOUT
#define CONFIG_POWER_BUTTON_INIT_TIMEOUT 4

/* GPIO mapping from board specific name to EC common name. */
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_EC_BATT_PRES_ODL
#define CONFIG_SCI_GPIO GPIO_EC_FCH_SCI_ODL
#define GPIO_AC_PRESENT GPIO_ACOK_OD
#define GPIO_CPU_PROCHOT GPIO_PROCHOT_ODL
#define GPIO_EC_INT_L GPIO_EC_AP_INT_ODL
#define GPIO_ENABLE_BACKLIGHT_L GPIO_EC_EDP_BL_DISABLE
#define GPIO_ENTERING_RW GPIO_EC_ENTERING_RW
#define GPIO_KBD_KSO2 GPIO_EC_KSO_02_INV
#define GPIO_PCH_PWRBTN_L GPIO_EC_FCH_PWR_BTN_L
#define GPIO_PCH_RSMRST_L GPIO_EC_FCH_RSMRST_L
#define GPIO_PCH_SLP_S3_L GPIO_SLP_S3_L
#define GPIO_PCH_SLP_S5_L GPIO_SLP_S5_L
#define GPIO_PCH_SYS_PWROK GPIO_EC_FCH_PWROK
#define GPIO_PCH_WAKE_L GPIO_EC_FCH_WAKE_L
#define GPIO_POWER_BUTTON_L GPIO_EC_PWR_BTN_ODL
#define GPIO_S0_PGOOD GPIO_S0_PWROK_OD
#define GPIO_S5_PGOOD GPIO_EC_PWROK_OD
#define GPIO_SYS_RESET_L GPIO_EC_SYS_RST_L
#define GPIO_VOLUME_DOWN_L GPIO_VOLDN_BTN_ODL
#define GPIO_VOLUME_UP_L GPIO_VOLUP_BTN_ODL
#define GPIO_WP_L GPIO_EC_WP_L
#define GPIO_PACKET_MODE_EN GPIO_EC_H1_PACKET_MODE

#ifndef __ASSEMBLER__

/* This I2C moved. Temporarily detect and support the V0 HW. */
extern int I2C_PORT_BATTERY;

enum adc_channel { ADC_TEMP_SENSOR_CHARGER, ADC_TEMP_SENSOR_SOC, ADC_CH_COUNT };

enum battery_type {
	BATTERY_SMP,
	BATTERY_SMP_1,
	BATTERY_SMP_2,
	BATTERY_SMP_3,
	BATTERY_LGC,
	BATTERY_LGC_1,
	BATTERY_LGC_2,
	BATTERY_CEL,
	BATTERY_CEL_1,
	BATTERY_SUNWODA,
	BATTERY_SUNWODA_1,
	BATTERY_TYPE_COUNT,
};

enum pwm_channel { PWM_CH_KBLIGHT = 0, PWM_CH_COUNT };

enum ioex_port { IOEX_C0_NCT3807 = 0, IOEX_PORT_COUNT };

#define PORT_TO_HPD(port) ((port == 0) ? GPIO_USB3_C0_DP2_HPD : GPIO_DP1_HPD)

enum temp_sensor_id {
	TEMP_SENSOR_CHARGER = 0,
	TEMP_SENSOR_SOC,
	TEMP_SENSOR_CPU,
	TEMP_SENSOR_COUNT
};

enum usba_port { USBA_PORT_A0 = 0, USBA_PORT_A1, USBA_PORT_COUNT };

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_COUNT };

/*****************************************************************************
 * CBI EC FW Configuration
 */

/**
 * VILBOZ_MB_USBAC
 *	USB-A0  Speed: 5 Gbps
 *		Retimer: none
 *	USB-C0  Speed: 5 Gbps
 *		Retimer: none
 *		TCPC: NCT3807
 *		PPC: AOZ1380
 *		IOEX: TCPC
 */
enum ec_cfg_usb_mb_type {
	VILBOZ_MB_USBAC = 0,
};

/**
 * VILBOZ_DB_D_OPT1_USBA_HDMI
 *	USB-A1  Speed: 5 Gbps
 *		Retimer: None
 *	HDMI    Retimer: PS8203
 *		MST Hub: none
 *	P-Sensor SX9324
 */
enum ec_cfg_usb_db_type {
	VILBOZ_DB_D_OPT1_USBA_HDMI = 0,
};

#include "cbi_ec_fw_config.h"

void board_reset_pd_mcu(void);

/* Common definition for the USB PD interrupt handlers. */
void tcpc_alert_event(enum gpio_signal signal);
void bc12_interrupt(enum gpio_signal signal);
void ppc_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
