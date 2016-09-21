/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IT83xx development board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* NOTE: 0->ec evb, non-zero->pd evb */
#define IT83XX_PD_EVB  0

/* Optional features */
#define CONFIG_BATTERY_SMART
#define CONFIG_BOARD_VERSION
#define CONFIG_FANS 1
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_IT83XX_SMCLK2_ON_GPC7
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_LOW_POWER_S0
#define CONFIG_PECI_TJMAX 100
#define CONFIG_POWER_BUTTON
/* Use CS0 of SSPI */
#define CONFIG_SPI_FLASH_PORT 0
#define CONFIG_UART_HOST

#if IT83XX_PD_EVB
/* PD */
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CHECK_MAX_REQUEST_ALLOWED
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_PORT_COUNT    2
#define CONFIG_USB_PD_TCPM_ITE83XX
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_GPIO
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#undef CONFIG_UART_HOST
#endif

/* Optional console commands */
#define CONFIG_CMD_FLASH
#define CONFIG_CMD_SCRATCHPAD
#define CONFIG_CMD_STACKOVERFLOW

/* Debug */
#undef CONFIG_CMD_FORCETIME
#undef CONFIG_HOOK_DEBUG
#undef CONFIG_KEYBOARD_DEBUG
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

#define I2C_PORT_CHARGER IT83XX_I2C_CH_C
#define I2C_PORT_BATTERY IT83XX_I2C_CH_C

#include "gpio_signal.h"

enum pwm_channel {
	PWM_CH_FAN,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum adc_channel {
	ADC_VBUSSA,
	ADC_VBUSSB,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

enum ec2i_setting {
	EC2I_SET_KB_LDN,
	EC2I_SET_KB_IRQ,
	EC2I_SET_KB_ENABLE,
	EC2I_SET_MOUSE_LDN,
	EC2I_SET_MOUSE_IRQ,
	EC2I_SET_MOUSE_ENABLE,
	EC2I_SET_PMC1_LDN,
	EC2I_SET_PMC1_IRQ,
	EC2I_SET_PMC1_ENABLE,
	EC2I_SET_PMC2_LDN,
	EC2I_SET_PMC2_BASE0_MSB,
	EC2I_SET_PMC2_BASE0_LSB,
	EC2I_SET_PMC2_BASE1_MSB,
	EC2I_SET_PMC2_BASE1_LSB,
	EC2I_SET_PMC2_IRQ,
	EC2I_SET_PMC2_ENABLE,
	EC2I_SET_SMFI_LDN,
	EC2I_SET_SMFI_H2RAM_IO_BASE,
	EC2I_SET_SMFI_H2RAM_MAP_LPC_IO,
	EC2I_SET_SMFI_ENABLE,
	EC2I_SET_PMC3_LDN,
	EC2I_SET_PMC3_BASE0_MSB,
	EC2I_SET_PMC3_BASE0_LSB,
	EC2I_SET_PMC3_BASE1_MSB,
	EC2I_SET_PMC3_BASE1_LSB,
	EC2I_SET_PMC3_IRQ,
	EC2I_SET_PMC3_ENABLE,
	EC2I_SET_RTCT_LDN,
	EC2I_SET_RTCT_P80LB,
	EC2I_SET_RTCT_P80LE,
	EC2I_SET_RTCT_P80LC,
#ifdef CONFIG_UART_HOST
	EC2I_SET_UART2_LDN,
	EC2I_SET_UART2_IO_BASE_MSB,
	EC2I_SET_UART2_IO_BASE_LSB,
	EC2I_SET_UART2_IRQ,
	EC2I_SET_UART2_IRQ_TYPE,
	EC2I_SET_UART2_ENABLE,
#endif
	/* Number of EC2I settings */
	EC2I_SETTING_COUNT
};

#if IT83XX_PD_EVB
/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW       60000
#define PD_MAX_CURRENT_MA     3000
/* Try to negotiate to 20V since i2c noise problems should be fixed. */
#define PD_MAX_VOLTAGE_MV     20000
/* start as a sink in case we have no other power supply/battery */
#define PD_DEFAULT_STATE PD_STATE_SNK_DISCONNECTED
/* TODO: determine the following board specific type-C power constants */
/*
 * delay to turn on the power supply max is ~16ms.
 * delay to turn off the power supply max is about ~180ms.
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* delay to turn on/off vconn */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

int board_get_battery_soc(void);
void board_pd_vconn_ctrl(int port, int cc_pin, int enabled);
void board_pd_vbus_ctrl(int port, int enabled);
#endif

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
