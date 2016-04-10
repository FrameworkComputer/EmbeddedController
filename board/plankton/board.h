/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Plankton board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART2 (PA14/PA15) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2

/* Optional features */
#define CONFIG_STM_HWTIMER32
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#undef  CONFIG_USB_PD_COMM_ENABLED
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DYNAMIC_SRC_CAP
#define CONFIG_USB_PD_IDENTITY_HW_VERS 1
#define CONFIG_USB_PD_IDENTITY_SW_VERS 1
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_PORT_COUNT 1
#define CONFIG_USB_PD_TCPC
#define CONFIG_USB_PD_TCPM_STUB
#define CONFIG_ADC
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_INA219
#define CONFIG_IO_EXPANDER_PCA9534
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_TASK_PROFILING

/* I2C ports configuration */
#define I2C_PORT_MASTER 1

/* USB configuration */
#define CONFIG_USB_PID 0x500c
#define CONFIG_USB_BCD_DEV 0x0001 /* v 0.01 */

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#undef DEFERRABLE_MAX_COUNT
#define DEFERRABLE_MAX_COUNT 9

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC     3

#include "gpio_signal.h"

/* ADC signal */
enum adc_channel {
	ADC_CH_CC1_PD = 0,
	ADC_CH_CC2_PD,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

enum board_src_cap {
	SRC_CAP_5V = 0,
	SRC_CAP_12V,
	SRC_CAP_20V,
};

/* 3.0A Rp */
#define PD_SRC_VNC            PD_SRC_3_0_VNC_MV
#define PD_SNK_RD_THRESHOLD   PD_SRC_3_0_RD_THRESH_MV

/* we are acting only as a sink */
#define PD_DEFAULT_STATE PD_STATE_SNK_DISCONNECTED

/* delay necessary for the voltage transition on the power supply */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  50000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 50000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 5000
#define PD_MAX_POWER_MW       60000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_VOLTAGE_MV     20000

/* Set USB PD source capability */
void board_set_source_cap(enum board_src_cap cap);

/* Reset USB hub if USB hub is switched to type-C port */
void board_maybe_reset_usb_hub(void);

/* Get fake ADC reading */
int board_fake_pd_adc_read(int cc);

/* Set pull-up/pull-down on CC lines */
void board_pd_set_host_mode(int enable);

/*
 * Whether the board is in USB hub mode or not
 *
 * @return 1 when in hub mode, 0 when not, and -1 on error.
 */
int board_in_hub_mode(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
