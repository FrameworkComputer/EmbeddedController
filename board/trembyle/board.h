/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trembyle board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_ZORK_TREMBYLE

#include <stdbool.h>
#include "baseboard.h"

#define CONFIG_USBC_RETIMER_PI3DPX1207
#define CONFIG_MKBP_USE_GPIO

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

enum adc_channel {
	ADC_TEMP_SENSOR_CHARGER,
	ADC_TEMP_SENSOR_SOC,
	ADC_CH_COUNT
};

enum battery_type {
	BATTERY_AP18F4M,
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
 * TREMBYLE_MB_USBAC
 *	USB-A0  Speed: 10 Gbps
 *		Retimer: PS8811
 *	USB-C0  Speed: 10 Gbps
 *		Retimer: PI3DPX1207
 *		TCPC: NCT3807
 *		PPC: AOZ1380
 *		IOEX: TCPC
 */
enum ec_cfg_usb_mb_type {
	TREMBYLE_MB_USBAC = 0,
};

/**
 * TREMBYLE_DB_T_OPT1_USBAC_HMDI
 *	USB-A1  Speed: 10 Gbps
 *		Retimer: PS8811
 *	USB-C1  Speed: 10 Gbps
 *		Retimer: PS8818
 *		TCPC: NCT3807
 *		PPC: NX20P3483
 *		IOEX: TCPC
 *	HDMI    Exists: yes
 *		Retimer: PI3HDX1204
 *		MST Hub: none
 *
 * TREMBYLE_DB_T_OPT2_USBAC
 *	USB-A1  Speed: 10 Gbps
 *		Retimer: PS8811
 *	USB-C1  Speed: 10 Gbps
 *		Retimer: PS8802
 *		TCPC: NCT3807
 *		PPC: NX20P3483
 *		IOEX: TCPC
 *	HDMI    Exists: no
 *		Retimer: none
 *		MST Hub: none
 *
 * TREMBYLE_DB_T_OPT3_USBAC_HDMI_MSTHUB
 *	USB-A1  Speed: 10 Gbps
 *		Retimer: PS8811
 *	USB-C1  Speed: 10 Gbps
 *		Retimer: PS8802
 *		TCPC: NCT3807
 *		PPC: NX20P3483
 *		IOEX: TCPC
 *	HDMI    Exists: yes
 *		Retimer: none
 *		MST Hub: RTD2141B
 */
enum ec_cfg_usb_db_type {
	TREMBYLE_DB_T_OPT1_USBAC_HMDI = 0,
	TREMBYLE_DB_T_OPT2_USBAC = 1,
	TREMBYLE_DB_T_OPT3_USBAC_HDMI_MSTHUB = 2,
};


#define HAS_USBC1_RETIMER_PS8802 \
			(BIT(TREMBYLE_DB_T_OPT2_USBAC) | \
			 BIT(TREMBYLE_DB_T_OPT3_USBAC_HDMI_MSTHUB))

static inline bool ec_config_has_usbc1_retimer_ps8802(void)
{
	return !!(BIT(ec_config_get_usb_db()) &
		  HAS_USBC1_RETIMER_PS8802);
}

#define HAS_USBC1_RETIMER_PS8818 \
			(BIT(TREMBYLE_DB_T_OPT1_USBAC_HMDI))

static inline bool ec_config_has_usbc1_retimer_ps8818(void)
{
	return !!(BIT(ec_config_get_usb_db()) &
		  HAS_USBC1_RETIMER_PS8818);
}

#define HAS_HDMI_RETIMER_PI3HDX1204 \
			(BIT(TREMBYLE_DB_T_OPT1_USBAC_HMDI))

static inline bool ec_config_has_hdmi_retimer_pi3hdx1204(void)
{
	return !!(BIT(ec_config_get_usb_db()) &
		  HAS_HDMI_RETIMER_PI3HDX1204);
}

#define HAS_MST_HUB_RTD2141B \
			(BIT(TREMBYLE_DB_T_OPT3_USBAC_HDMI_MSTHUB))

static inline bool ec_config_has_mst_hub_rtd2141b(void)
{
	return !!(BIT(ec_config_get_usb_db()) &
		  HAS_MST_HUB_RTD2141B);
}

#define HAS_HDMI_CONN_HPD \
			(BIT(TREMBYLE_DB_T_OPT1_USBAC_HMDI))

static inline bool ec_config_has_hdmi_conn_hpd(void)
{
	return !!(BIT(ec_config_get_usb_db()) &
		  HAS_HDMI_CONN_HPD);
}

#define PORT_TO_HPD(port) ((port == 0) \
	? GPIO_USB_C0_HPD \
	: (ec_config_has_usbc1_retimer_ps8802()) \
		? GPIO_DP1_HPD \
		: GPIO_DP2_HPD)

extern const struct usb_mux usbc0_pi3dpx1207_usb_retimer;
extern const struct usb_mux usbc1_ps8802;
extern const struct usb_mux usbc1_ps8818;
extern struct usb_mux usbc1_amd_fp5_usb_mux;

void hdmi_hpd_interrupt(enum ioex_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
