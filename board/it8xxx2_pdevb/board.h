/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* IT8xxx2 PD development board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#include "baseboard.h"

/*
 * Enable PD in RO image for TCPMv2, otherwise there is only Type-c functions.
 * NOTE: This configuration is only for development board and will never be
 *       released on a chrome os device.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* EC configurations, unnecessarily for PD */
#undef CONFIG_FANS
#undef CONFIG_IT83XX_ENABLE_MOUSE_DEVICE
#undef CONFIG_IT83XX_SMCLK2_ON_GPC7
#undef CONFIG_KEYBOARD_BOARD_CONFIG
#undef CONFIG_KEYBOARD_PROTOCOL_8042
#undef CONFIG_PECI
#undef CONFIG_PECI_COMMON
#undef CONFIG_PECI_TJMAX
#undef CONFIG_POWER_BUTTON
#undef CONFIG_PWM
#undef CONFIG_SPI_MASTER
#undef CONFIG_SPI_FLASH_PORT
#undef CONFIG_UART_HOST
#undef CONFIG_HOSTCMD_LPC
#undef CONFIG_CMD_MMAPINFO
#undef CONFIG_SWITCH

/* PD */
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CHECK_MAX_REQUEST_ALLOWED
#define CONFIG_USB_PD_CUSTOM_PDO
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_PORT_MAX_COUNT    2
#define CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT   2
#define CONFIG_USB_PD_TCPMV2
#define CONFIG_USB_DRP_ACC_TRYSRC
#define CONFIG_USB_PD_REV30
#define CONFIG_USB_PID 0x1234            /* Invalid PID for development board */
#define CONFIG_USB_PD_DEBUG_LEVEL 2
#define CONFIG_USB_PD_TCPM_ITE_ON_CHIP
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_GPIO
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_DECODE_SOP
#define CONFIG_VBOOT_HASH

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum pwm_channel {
	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum adc_channel {
	ADC_VBUSSA,
	ADC_VBUSSB,
	ADC_VBUSSC,
	ADC_EVB_CH_13,
	ADC_EVB_CH_14,
	ADC_EVB_CH_15,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW       60000
#define PD_MAX_CURRENT_MA     3000
/* Try to negotiate to 20V since i2c noise problems should be fixed. */
#define PD_MAX_VOLTAGE_MV     20000
/* TODO: determine the following board specific type-C power constants */
/*
 * delay to turn on the power supply max is ~16ms.
 * delay to turn off the power supply max is about ~180ms.
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* delay to turn on/off vconn */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

void board_pd_vbus_ctrl(int port, int enabled);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
