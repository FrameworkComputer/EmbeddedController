/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_ZORK_TREMBYLE

#include <stdbool.h>
#include "baseboard.h"

#define CONFIG_MKBP_USE_GPIO
#define CONFIG_FAN_RPM_CUSTOM

/* Motion sensing drivers */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCEL_KX022
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_TABLET_MODE
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

/* Type C mux/retimer */
#define CONFIG_USB_MUX_PS8743
#define CONFIG_USBC_RETIMER_TUSB544
#define TUSB544_I2C_ADDR_FLAGS1 0x0F

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
#define GPIO_DP1_HPD			GPIO_EC_DP1_HPD
#define IOEX_HDMI_CONN_HPD_3V3_DB	IOEX_USB_C1_PPC_ILIM_3A_EN

#ifndef __ASSEMBLER__

enum adc_channel {
	ADC_TEMP_SENSOR_CHARGER,
	ADC_TEMP_SENSOR_SOC,
	ADC_CH_COUNT
};

enum battery_type {
	BATTERY_AP19B8M,
	BATTERY_AP18C7M,
	BATTERY_TYPE_COUNT,
};

enum mft_channel {
	MFT_CH_0 = 0,
	/* Number of MFT channels */
	MFT_CH_COUNT,
};

enum pwm_channel {
	PWM_CH_KBLIGHT = 0,
	PWM_CH_FAN,
	PWM_CH_COUNT
};

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

/*****************************************************************************
 * CBI EC FW Configuration
 */
#include "cbi_ec_fw_config.h"

/**
 * EZKINIL_MB_USBAC
 *	USB-A0  Speed: 5 Gbps
 *		Retimer: none
 *	USB-C0  Speed: 5 Gbps
 *		Retimer: none
 *		TCPC: NCT3807
 *		PPC: AOZ1380
 *		IOEX: TCPC
 */
enum ec_cfg_usb_mb_type {
	EZKINIL_MB_USBAC = 0,
};

/**
 * EZKINIL_DB_T_OPT1_USBC_HDMI
 *	USB-A1  none
 *	USB-C1  Speed: 5 Gbps
 *		Retimer: TUSB544
 *		TCPC: NCT3807
 *		PPC: NX20P3483
 *		IOEX: TCPC
 *	HDMI    Exists: yes
 *		Retimer: PI3HDX1204
 *		MST Hub: none
 *
 * EZKINIL_DB_T_OPT2_USBAC
 *	USB-A1  Speed: 5 Gbps
 *		Retimer: TUSB522
 *	USB-C1  Speed: 5 Gbps
 *		Retimer: PS8743
 *		TCPC: NCT3807
 *		PPC: NX20P3483
 *		IOEX: TCPC
 *	HDMI    Exists: no
 *		Retimer: none
 *		MST Hub: none
 */
enum ec_cfg_usb_db_type {
	EZKINIL_DB_T_OPT1_USBC_HDMI = 0,
	EZKINIL_DB_T_OPT2_USBAC = 1,
};


#define HAS_USBA1_RETIMER_TUSB522 \
			(BIT(EZKINIL_DB_T_OPT2_USBAC))

static inline bool ec_config_has_usba1_retimer_tusb522(void)
{
	return !!(BIT(ec_config_get_usb_db()) &
		  HAS_USBA1_RETIMER_TUSB522);
}

#define HAS_USBC1_RETIMER_PS8743 \
			(BIT(EZKINIL_DB_T_OPT2_USBAC))

static inline bool ec_config_has_usbc1_retimer_ps8743(void)
{
	return !!(BIT(ec_config_get_usb_db()) &
		  HAS_USBC1_RETIMER_PS8743);
}

#define HAS_USBC1_RETIMER_TUSB544 \
			(BIT(EZKINIL_DB_T_OPT1_USBC_HDMI))

static inline bool ec_config_has_usbc1_retimer_tusb544(void)
{
	return !!(BIT(ec_config_get_usb_db()) &
		  HAS_USBC1_RETIMER_TUSB544);
}

#define HAS_HDMI_RETIMER_PI3HDX1204 \
			(BIT(EZKINIL_DB_T_OPT1_USBC_HDMI))

static inline bool ec_config_has_hdmi_retimer_pi3hdx1204(void)
{
	return !!(BIT(ec_config_get_usb_db()) &
		  HAS_HDMI_RETIMER_PI3HDX1204);
}

#define HAS_HDMI_CONN_HPD \
			(BIT(EZKINIL_DB_T_OPT1_USBC_HDMI))

static inline bool ec_config_has_hdmi_conn_hpd(void)
{
	return !!(BIT(ec_config_get_usb_db()) &
		  HAS_HDMI_CONN_HPD);
}

/* TODO: Fill in with GPIO values */
#define PORT_TO_HPD(port) ((port == 0) \
	? GPIO_USB_C0_HPD \
	: (ec_config_has_usbc1_retimer_ps8743()) \
		? GPIO_DP1_HPD \
		: GPIO_DP2_HPD)

extern const struct usb_mux usbc1_tusb544;
extern const struct usb_mux usbc1_ps8743;
extern struct usb_mux usbc1_amd_fp5_usb_mux;

void hdmi_hpd_interrupt(enum gpio_signal signal);
void hdmi_hpd_interrupt_v2(enum ioex_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
