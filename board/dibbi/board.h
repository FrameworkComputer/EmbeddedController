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

/* Battery */
#define CONFIG_BATTERY_FUEL_GAUGE

/* BC 1.2 */
#define CONFIG_BC12_DETECT_PI3USB9201

/* Charger */
#define CONFIG_CHARGER_SM5803 /* C0: Charger */
#define PD_MAX_VOLTAGE_MV 15000
#define CONFIG_USB_PD_VBUS_DETECT_CHARGER
#define CONFIG_USB_PD_5V_CHARGER_CTRL
#define CONFIG_CHARGER_OTG

/* LED */
#define CONFIG_LED_PWM
#define CONFIG_LED_PWM_COUNT 1

/* PWM */
#define CONFIG_PWM

/* TCPC */
#define CONFIG_USB_PD_PORT_MAX_COUNT 1
#define CONFIG_USB_PD_TCPM_ITE_ON_CHIP /* C0: ITE EC TCPC */
#define CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT 1

/* Power: Dedicated barreljack charger port */
#undef CONFIG_DEDICATED_CHARGE_PORT_COUNT
#define CONFIG_DEDICATED_CHARGE_PORT_COUNT 1
#define DEDICATED_CHARGE_PORT 1

/* Thermistors */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

/* USB Mux and Retimer */
#define CONFIG_USB_MUX_IT5205 /* C0: ITE Mux */
#define I2C_PORT_USB_MUX I2C_PORT_USB_C0 /* Required for ITE Mux */

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

/* Buttons */
#define CONFIG_DEDICATED_RECOVERY_BUTTON
#define CONFIG_DEDICATED_RECOVERY_BUTTON_2
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_IGNORE_LID
#define CONFIG_POWER_BUTTON_X86

/* Unused features - Misc */
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

enum pwm_channel {
	PWM_CH_KBLIGHT,
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
	ADC_SUB_ANALOG, /* ADC13 */
	ADC_CH_COUNT
};

enum temp_sensor_id { TEMP_SENSOR_1, TEMP_SENSOR_2, TEMP_SENSOR_COUNT };

/* List of possible batteries */
enum battery_type {
	BATTERY_LGC15,
	BATTERY_PANASONIC_AP15O5L,
	BATTERY_SANYO,
	BATTERY_SONY,
	BATTERY_SMP_AP13J7K,
	BATTERY_PANASONIC_AC15A3J,
	BATTERY_LGC_AP18C8K,
	BATTERY_MURATA_AP18C4K,
	BATTERY_LGC_AP19A8K,
	BATTERY_LGC_G023,
	BATTERY_TYPE_COUNT,
};

/* Board specific handlers */
/* TODO(b/257377326) Update this with power re-work */
#define PORT_TO_HPD(port) (GPIO_USB_C0_DP_HPD)

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
