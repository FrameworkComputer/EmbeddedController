/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Palkia board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

#define CONFIG_POWER_BUTTON
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_LED_COMMON
#define CONFIG_LED_POWER_LED
#define CONFIG_LOW_POWER_IDLE

#define CONFIG_HOSTCMD_ESPI

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

/* Keyboard features */
#define CONFIG_PWM_KBLIGHT
#define CONFIG_KEYBOARD_CUSTOMIZATION

/* Enable board_config_pre_init() */
#define CONFIG_BOARD_PRE_INIT

/* USB Type C and USB PD defines */
#define CONFIG_USB_PD_COMM_LOCKED
#define CONFIG_USB_PD_TCPM_PS8751
#define BOARD_TCPC_C0_RESET_HOLD_DELAY PS8XXX_RESET_DELAY_MS
#define BOARD_TCPC_C0_RESET_POST_DELAY 0
#define GPIO_USB_C0_TCPC_RST GPIO_USB_C0_TCPC_RST_ODL

/* USB Type A Features */
#define CONFIG_USB_PORT_POWER_SMART
#undef CONFIG_USB_PORT_POWER_SMART_PORT_COUNT
#define CONFIG_USB_PORT_POWER_SMART_PORT_COUNT 1
#define CONFIG_USB_PORT_POWER_SMART_CDP_SDP_ONLY
#define GPIO_USB1_ILIM_SEL GPIO_USB_A_LOW_PWR_ODL

/*
 * Palkia' battery takes several seconds to come back out of its disconnect
 * state (~4.2 seconds on the unit I have, so give it a little more for margin).
 */
#undef  CONFIG_POWER_BUTTON_INIT_TIMEOUT
#define CONFIG_POWER_BUTTON_INIT_TIMEOUT 6

/* BC 1.2 */
#define CONFIG_BC12_DETECT_PI3USB9201

/* Fan features */
#define CONFIG_FANS 1
#undef CONFIG_FAN_INIT_SPEED
#define CONFIG_FAN_INIT_SPEED 50
#define CONFIG_TEMP_SENSOR_POWER_GPIO GPIO_EN_A_RAILS
#define CONFIG_THERMISTOR
#define CONFIG_THROTTLE_AP
#define CONFIG_STEINHART_HART_3V3_30K9_47K_4050B

/* DPTF */
#define CONFIG_DPTF_MULTI_PROFILE

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_PCH_RSMRST_L	GPIO_EC_PCH_RSMRST_L
#define GPIO_PCH_SLP_S0_L	GPIO_SLP_S0_L
#define GPIO_CPU_PROCHOT	GPIO_EC_PROCHOT_ODL
#define GPIO_AC_PRESENT		GPIO_ACOK_OD
#define GPIO_RSMRST_L_PGOOD	GPIO_PG_EC_RSMRST_L
#define GPIO_PCH_SYS_PWROK	GPIO_EC_PCH_SYS_PWROK
#define GPIO_PCH_SLP_S3_L	GPIO_SLP_S3_L
#define GPIO_PCH_SLP_S4_L	GPIO_SLP_S4_L
#define GPIO_EN_PP5000		GPIO_EN_PP5000_A

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_1,	/* ADC0 */
	ADC_TEMP_SENSOR_2,	/* ADC1 */
	ADC_TEMP_SENSOR_3,	/* ADC3 */
	ADC_TEMP_SENSOR_4,	/* ADC2 */
	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_KBLIGHT,
	PWM_CH_FAN,
	PWM_CH_COUNT
};

enum fan_channel {
	FAN_CH_0 = 0,
	/* Number of FAN channels */
	FAN_CH_COUNT,
};

enum mft_channel {
	MFT_CH_0 = 0,
	/* Number of MFT channels */
	MFT_CH_COUNT,
};

enum temp_sensor_id {
	TEMP_SENSOR_1,
	TEMP_SENSOR_2,
	TEMP_SENSOR_3,
	TEMP_SENSOR_4,
	TEMP_SENSOR_COUNT
};

/* List of possible batteries */
enum battery_type {
	BATTERY_DYNAPACK_UX48144,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
