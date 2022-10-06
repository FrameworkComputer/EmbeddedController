/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Morphius board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_ZORK_TREMBYLE

#include <stdbool.h>
#include "baseboard.h"

#define CONFIG_USBC_RETIMER_PI3DPX1207
#define CONFIG_8042_AUX
#define CONFIG_PS2
#define CONFIG_CMD_PS2
#define CONFIG_KEYBOARD_FACTORY_TEST
#define CONFIG_DEVICE_EVENT
#define CONFIG_ASSERT_CCD_MODE_ON_DTS_CONNECT

#undef CONFIG_LED_ONOFF_STATES
#define CONFIG_BATTERY_LEVEL_NEAR_FULL 91

#undef ZORK_PS8818_RX_INPUT_TERM
#define ZORK_PS8818_RX_INPUT_TERM PS8818_RX_INPUT_TERM_85_OHM

/* Motion sensing drivers */
#define CONFIG_ACCELGYRO_ICM426XX
#define CONFIG_ACCELGYRO_ICM426XX_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCEL_KX022
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_CUSTOM_FAN_CONTROL
#define CONFIG_TABLET_MODE
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_TMP432
#define CONFIG_TEMP_SENSOR_POWER
#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL
#define CONFIG_GMR_TABLET_MODE
#define CONFIG_GMR_TABLET_MODE_CUSTOM
#define GPIO_TABLET_MODE_L GPIO_TABLET_MODE
#define RPM_DEVIATION 1

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
#define GPIO_TEMP_SENSOR_POWER GPIO_EN_PWR_A
#define GPIO_VOLUME_DOWN_L GPIO_VOLDN_BTN_ODL
#define GPIO_VOLUME_UP_L GPIO_VOLUP_BTN_ODL
#define GPIO_WP_L GPIO_EC_WP_L
#define GPIO_PACKET_MODE_EN GPIO_EC_H1_PACKET_MODE

/* I2C mapping from board specific function*/
#define I2C_PORT_THERMAL I2C_PORT_AP_HDMI

#ifndef __ASSEMBLER__

void ps2_pwr_en_interrupt(enum gpio_signal signal);

enum adc_channel {
	ADC_TEMP_SENSOR_CHARGER,
	ADC_TEMP_SENSOR_5V_REGULATOR,
	ADC_CH_COUNT
};

enum battery_type {
	BATTERY_SMP,
	BATTERY_SUNWODA,
	BATTERY_LGC,
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
	PWM_CH_POWER_LED,
	PWM_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_CHARGER = 0,
	TEMP_SENSOR_5V_REGULATOR,
	TEMP_SENSOR_CPU,
	TEMP_SENSOR_SSD,
	TEMP_SENSOR_COUNT
};

enum usba_port { USBA_PORT_A0 = 0, USBA_PORT_A1, USBA_PORT_COUNT };

/*****************************************************************************
 * CBI EC FW Configuration
 */

/**
 * MORPHIUS_MB_USBAC
 *	USB-A0  Speed: 5 Gbps
 *		Retimer: none
 *	USB-C0  Speed: 5 Gbps
 *		Retimer: PI3DPX1207
 *		TCPC: NCT3807
 *		PPC: AOZ1380
 *		IOEX: TCPC
 */
enum ec_cfg_usb_mb_type {
	MORPHIUS_MB_USBAC = 0,
};

/**
 * MORPHIUS_DB_T_OPT1_USBC_HDMI
 *	USB-A1  none
 *	USB-C1  Speed: 5 Gbps
 *		Retimer: PS8818
 *		TCPC: NCT3807
 *		PPC: NX20P3483
 *		IOEX: TCPC
 *	HDMI    Exists: yes
 *		Retimer: PI3HDX1204
 *		MST Hub: none
 *
 * MORPHIUS_DB_T_OPT3_USBC_HDMI_MSTHUB
 *	USB-A1  none
 *	USB-C1  Speed: 5 Gbps
 *		Retimer: PS8802
 *		TCPC: NCT3807
 *		PPC: NX20P3483
 *		IOEX: TCPC
 *	HDMI    Exists: yes
 *		Retimer: none
 *		MST Hub: RTD2141B
 */
enum ec_cfg_usb_db_type {
	MORPHIUS_DB_T_OPT1_USBC_HDMI = 0,
	MORPHIUS_DB_T_OPT3_USBC_HDMI_MSTHUB = 1,
};

#include "cbi_ec_fw_config.h"

#define HAS_USBC1_RETIMER_PS8802 (BIT(MORPHIUS_DB_T_OPT3_USBC_HDMI_MSTHUB))

static inline bool ec_config_has_usbc1_retimer_ps8802(void)
{
	return !!(BIT(ec_config_get_usb_db()) & HAS_USBC1_RETIMER_PS8802);
}

#define HAS_USBC1_RETIMER_PS8818 (BIT(MORPHIUS_DB_T_OPT1_USBC_HDMI))

static inline bool ec_config_has_usbc1_retimer_ps8818(void)
{
	return !!(BIT(ec_config_get_usb_db()) & HAS_USBC1_RETIMER_PS8818);
}

#define HAS_HDMI_RETIMER_PI3HDX1204 (BIT(MORPHIUS_DB_T_OPT1_USBC_HDMI))

static inline bool ec_config_has_hdmi_retimer_pi3hdx1204(void)
{
	return !!(BIT(ec_config_get_usb_db()) & HAS_HDMI_RETIMER_PI3HDX1204);
}

#define HAS_MST_HUB_RTD2141B (BIT(MORPHIUS_DB_T_OPT3_USBC_HDMI_MSTHUB))

static inline bool ec_config_has_mst_hub_rtd2141b(void)
{
	return !!(BIT(ec_config_get_usb_db()) & HAS_MST_HUB_RTD2141B);
}

void motion_interrupt(enum gpio_signal signal);

/**
 * @warning Callers must use gpio_or_ioex_set_level to handle the return result
 * since either type of signal can be returned.
 *
 * @return GPIO (gpio_signal) or IOEX (ioex_signal)
 */
int board_usbc_port_to_hpd_gpio_or_ioex(int port);
#define PORT_TO_HPD(port) board_usbc_port_to_hpd_gpio_or_ioex(port)

extern const struct usb_mux_chain usbc0_pi3dpx1207_usb_retimer;
extern const struct usb_mux usbc1_ps8818;
extern struct usb_mux usbc1_ps8802;
extern struct usb_mux usbc1_amd_fp5_usb_mux;

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
