/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Dibbi board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_DEDEDE_EC_IT8320
#include "baseboard.h"

/* System unlocked in early development */
#define CONFIG_SYSTEM_UNLOCKED

#define CONFIG_CMD_CHARGER_DUMP

/* Power */
#undef CONFIG_CHARGER
#undef CONFIG_CHARGER_DISCHARGE_ON_AC
#undef CONFIG_USB_PD_VBUS_MEASURE_CHARGER
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 16000
#define PD_MAX_VOLTAGE_MV 15000
#define CONFIG_USB_PD_VBUS_DETECT_GPIO
/* ADC sensors could measure VBUS on this board, but components are DNS */
#define CONFIG_USB_PD_VBUS_MEASURE_NOT_PRESENT

/* Override macro for C0 only */
#define PORT_TO_HPD(port) (GPIO_USB_C0_DP_HPD)

/* Power: Dedicated barreljack charger port */
#undef CONFIG_DEDICATED_CHARGE_PORT_COUNT
#define CONFIG_DEDICATED_CHARGE_PORT_COUNT 1
#define DEDICATED_CHARGE_PORT 1

/* USB Type-C */
#undef CONFIG_USB_CHARGER
#undef CONFIG_USB_MUX_PI3USB31532

/* TCPC */
#define CONFIG_USB_PD_PORT_MAX_COUNT 1
#define CONFIG_USB_PD_TCPM_ITE_ON_CHIP /* C0: ITE EC TCPC */
#define CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT 1

/* PPC */
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USB_PD_VBUS_DETECT_PPC
#define CONFIG_USBC_PPC
#define CONFIG_USBC_PPC_SYV682X

/* USB Mux and Retimer */
#define CONFIG_USB_MUX_IT5205 /* C0: ITE Mux */
#define I2C_PORT_USB_MUX I2C_PORT_USB_C0 /* Required for ITE Mux */

/* USB Type A Features */
#define CONFIG_USB_PORT_POWER_DUMB
#define USB_PORT_COUNT 4 /* Type A ports */

/* No battery */
#undef CONFIG_BATTERY_CUT_OFF
#undef CONFIG_BATTERY_PRESENT_GPIO
#undef CONFIG_BATTERY_REQUESTS_NIL_WHEN_DEAD
#undef CONFIG_BATTERY_REVIVE_DISCONNECT
#undef CONFIG_BATTERY_SMART

/* LED */
/* TODO(b/259467280) Determine what LED/PWM impl is needed*/
/* #define CONFIG_LED_PWM */
/* #define CONFIG_LED_PWM_COUNT 1 */

/* PWM */
#define CONFIG_PWM

/* Thermistors */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

/* Buttons */
#define CONFIG_DEDICATED_RECOVERY_BUTTON
#define CONFIG_DEDICATED_RECOVERY_BUTTON_2
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_IGNORE_LID
#define CONFIG_POWER_BUTTON_X86

/* No Keyboard */
#undef CONFIG_MKBP_EVENT
#undef CONFIG_MKBP_EVENT_WAKEUP_MASK
#undef CONFIG_MKBP_USE_GPIO_AND_HOST_EVENT
#undef CONFIG_KEYBOARD_COL2_INVERTED
#undef CONFIG_KEYBOARD_PROTOCOL_8042
#undef CONFIG_MKBP_INPUT_DEVICES
#undef CONFIG_CMD_KEYBOARD
#undef CONFIG_KEYBOARD_BOOT_KEYS
#undef CONFIG_KEYBOARD_RUNTIME_KEYS

/* No backlight */
#undef CONFIG_BACKLIGHT_LID
#undef GPIO_ENABLE_BACKLIGHT

/* Unused features - Misc */
#undef CONFIG_HIBERNATE
#undef CONFIG_VOLUME_BUTTONS
#undef CONFIG_LID_SWITCH
#undef CONFIG_TABLET_MODE
#undef CONFIG_TABLET_MODE_SWITCH
#undef CONFIG_GMR_TABLET_MODE
#undef GPIO_TABLET_MODE_L

/* Unused GPIOs */
#undef GPIO_USB_C1_DP_HPD

/* Pin renaming */
#define GPIO_RECOVERY_L GPIO_EC_RECOVERY_BTN_ODL
#define GPIO_RECOVERY_L_2 GPIO_H1_EC_RECOVERY_BTN_ODL
#define GPIO_POWER_BUTTON_L GPIO_H1_EC_PWR_BTN_ODL

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum charge_port {
	CHARGE_PORT_TYPEC0,
	CHARGE_PORT_BARRELJACK,
};

enum usbc_port { USBC_PORT_C0 = 0, USBC_PORT_COUNT };

enum pwm_channel {
	PWM_CH_LED_RED,
	PWM_CH_LED_GREEN,
	PWM_CH_LED_BLUE,
	PWM_CH_COUNT,
};

/* ADC channels */
enum adc_channel {
	ADC_VSNS_PP3300_A, /* ADC0 */
	ADC_TEMP_SENSOR_1, /* ADC2 */
	ADC_TEMP_SENSOR_2, /* ADC3 */
	ADC_PPVAR_PWR_IN_IMON, /* ADC15 */
	ADC_SNS_PPVAR_PWR_IN, /* ADC16 */
	ADC_CH_COUNT
};

enum temp_sensor_id { TEMP_SENSOR_1, TEMP_SENSOR_2, TEMP_SENSOR_COUNT };

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
