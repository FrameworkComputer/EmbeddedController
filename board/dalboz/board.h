/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trembyle board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_ZORK_DALBOZ

#include <stdbool.h>
#include "baseboard.h"

/*
 * Allow dangerous commands.
 * TODO: Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED
#define CONFIG_I2C_DEBUG

#define CONFIG_MKBP_USE_GPIO

/* Motion sensing drivers */
#define CONFIG_ACCELGYRO_LSM6DSM
#define CONFIG_ACCEL_LSM6DSM_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCEL_LIS2DWL
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_TABLET_MODE
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL



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

#ifndef __ASSEMBLER__

enum battery_type {
	BATTERY_SMP,
	BATTERY_LGC,
	BATTERY_CEL,
	BATTERY_TYPE_COUNT,
};

enum pwm_channel {
	PWM_CH_KBLIGHT = 0,
	PWM_CH_COUNT
};


/*****************************************************************************
 * CBI EC FW Configuration
 */
#include "cbi_ec_fw_config.h"

/**
 * DALBOZ_MB_USBAC
 *	USB-A0  Speed: 5 Gbps
 *		Retimer: none
 *	USB-C0  Speed: 5 Gbps
 *		Retimer: none
 *		TCPC: NCT3807
 *		PPC: AOZ1380
 *		IOEX: TCPC
 */
enum ec_cfg_usb_mb_type {
	DALBOZ_MB_USBAC = 0,
};

/**
 * DALBOZ_DB_D_OPT1_USBAC
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
 *
 * DALBOZ_DB_D_OPT2_USBA_HDMI
 *	USB-A1  Speed: 5 Gbps
 *		Retimer: TUSB522
 *	USB-C1  none
 *		IOEX: PCAL6408
 *	HDMI    Exists: yes
 *		Retimer: PI3HDX1204
 *		MST Hub: none
 */
enum ec_cfg_usb_db_type {
	DALBOZ_DB_D_OPT1_USBAC = 0,
	DALBOZ_DB_D_OPT2_USBA_HDMI = 1,
};


#define HAS_USBC1 \
			(BIT(DALBOZ_DB_D_OPT1_USBAC))

static inline bool ec_config_has_usbc1(void)
{
	return !!(BIT(ec_config_get_usb_db()) &
		  HAS_USBC1);
}


#define HAS_USBC1_RETIMER_PS8740 \
			(BIT(DALBOZ_DB_D_OPT1_USBAC))

static inline bool ec_config_has_usbc1_retimer_ps8740(void)
{
	return !!(BIT(ec_config_get_usb_db()) &
		  HAS_USBC1_RETIMER_PS8740);
}

/* These IO expander GPIOs vary with DB option. */
extern enum gpio_signal IOEX_USB_A1_RETIMER_EN;
extern enum gpio_signal IOEX_USB_A1_CHARGE_EN_DB_L;

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
