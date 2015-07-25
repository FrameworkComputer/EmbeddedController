/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* ryu board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* the UART console is on USART2 (PD4/PD5) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2

/* By default, enable all console messages excepted USB, lightbar and host */
#define CC_DEFAULT     (CC_ALL & ~(CC_MASK(CC_USB) | CC_MASK(CC_LIGHTBAR) |\
				   CC_MASK(CC_HOSTCMD)))

/* Optional features */
#undef CONFIG_CMD_HASH
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGE_RAMP_HW
#define CONFIG_FORCE_CONSOLE_RESUME
#define CONFIG_STM_HWTIMER32
#define CONFIG_USB_CHARGER
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CUSTOM_VDM
#undef  CONFIG_USB_PD_DEBUG_DR
#define CONFIG_USB_PD_DEBUG_DR PD_ROLE_UFP
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_FLASH_ERASE_CHECK
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_LOG_SIZE 512
#define CONFIG_USB_PD_PORT_COUNT 1
#define CONFIG_USB_PD_TCPC
#define CONFIG_USB_PD_TCPM_STUB
#define CONFIG_USB_SWITCH_PI3USB9281
#define CONFIG_USB_SWITCH_PI3USB9281_CHIP_COUNT 1
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_ADC
#define CONFIG_ADC_SAMPLE_TIME 3
#define CONFIG_HW_CRC
#define CONFIG_I2C
#define CONFIG_LID_SWITCH
#define CONFIG_LID_SWITCH_GPIO_LIST LID_GPIO(GPIO_LID_OPEN)\
				    LID_GPIO(GPIO_BASE_PRES_L)
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_MKBP_EVENT
#define CONFIG_VBOOT_HASH
#define CONFIG_WATCHDOG_HELP
#undef CONFIG_TASK_PROFILING
#define CONFIG_INDUCTIVE_CHARGING
#undef CONFIG_HIBERNATE
#undef CONFIG_UART_TX_DMA /* DMAC_CH7 is used by USB PD */
#define CONFIG_UART_RX_DMA
#define CONFIG_UART_RX_DMA_CH STM32_DMAC_USART2_RX

/* Charging/Power configuration */
#define CONFIG_BATTERY_RYU
#define CONFIG_BATTERY_BQ27541
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_REQUESTS_NIL_WHEN_DEAD
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGER_BQ25892
#define CONFIG_CHARGER_BQ2589X_IR_COMP (BQ2589X_IR_TREG_120C |    \
					BQ2589X_IR_VCLAMP_160MV | \
					BQ2589X_IR_BAT_COMP_60MOHM)
#define CONFIG_CHARGER_BQ2589X_BOOST (BQ2589X_BOOSTV_MV(4998) | \
				      BQ2589X_BOOST_LIM_1650MA)
#define CONFIG_CHARGER_ILIM_PIN_DISABLED
#define CONFIG_CHARGER_PROFILE_OVERRIDE
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHIPSET_TEGRA
#define CONFIG_PMIC_FW_LONG_PRESS_TIMER
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_ACTIVE_STATE 1
#define CONFIG_POWER_IGNORE_LID_OPEN

/* I2C ports configuration */
#define I2C_PORT_MASTER 0
#define I2C_PORT_SLAVE  1
#define I2C_PORT_EC I2C_PORT_SLAVE
#define I2C_PORT_CHARGER I2C_PORT_MASTER
#define I2C_PORT_BATTERY I2C_PORT_MASTER
#define I2C_PORT_LIGHTBAR I2C_PORT_MASTER
#define I2C_PORT_PERICOM I2C_PORT_MASTER

#define BMM150_I2C_ADDRESS BMM150_ADDR0

/* slave address for host commands */
#ifdef HAS_TASK_HOSTCMD
#define CONFIG_HOSTCMD_I2C_SLAVE_ADDR 0x3c
#endif

/* USART and USB stream drivers */
#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USART1
#define CONFIG_STREAM_USART3
#define CONFIG_STREAM_USB

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_PID 0x500f

/* Prevent the USB driver from initializing at boot */
#define CONFIG_USB_INHIBIT_INIT

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_CONSOLE   0
#define USB_IFACE_AP_STREAM 1
#define USB_IFACE_SH_STREAM 2
#define USB_IFACE_SPI       3
#define USB_IFACE_COUNT     4

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL   0
#define USB_EP_CONSOLE   1
#define USB_EP_AP_STREAM 2
#define USB_EP_SH_STREAM 3
#define USB_EP_SPI       4
#define USB_EP_COUNT     5

/* Enable console over USB */
#define CONFIG_USB_CONSOLE

#define CONFIG_SPI_MASTER
/* Enable control of SPI over USB */
#define CONFIG_SPI_FLASH_PORT    0  /* First SPI master port */
#define CONFIG_USB_SPI
/* Enable Case Closed Debugging */
#define CONFIG_CASE_CLOSED_DEBUG

/* Enable Accel over SPI */
#define CONFIG_SPI_ACCEL_PORT    1  /* Second SPI master port */
#define SPI_ACCEL_PORT_ID        1  /* stored at spi_ports[1] */

/* Sensor support */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_MAG_BMI160_BMM150
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

/* Size of FIFO queue is determined by Android Hifi sensor requirements:
 * Wake up sensors: Accel @50Hz + Barometer @5Hz + uncal mag @ 10Hz
 * 60s minimum, 3min recommened.
 * FIFO size is in power of 2.
 */
#define CONFIG_ACCEL_FIFO 2048

/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO / 3)

/* Maximum number of deferrable functions */
#undef  DEFERRABLE_MAX_COUNT
#define DEFERRABLE_MAX_COUNT 16

#ifndef __ASSEMBLER__

int board_get_version(void);

/* Timer selection */
#define TIM_CLOCK32 5
#define TIM_WATCHDOG 19

#include "gpio_signal.h"

enum power_signal {
	TEGRA_XPSHOLD = 0,
	TEGRA_SUSPEND_ASSERTED,

	/* Number of power signals */
	POWER_SIGNAL_COUNT
};

/* ADC signal */
enum adc_channel {
	ADC_VBUS = 0,
	ADC_CC1_PD,
	ADC_CC2_PD,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_VERSION,
	USB_STR_CONSOLE_NAME,
	USB_STR_AP_STREAM_NAME,
	USB_STR_SH_STREAM_NAME,

	USB_STR_COUNT
};

/* VBUS enable GPIO */
#define GPIO_USB_C0_5V_EN GPIO_CHGR_OTG

/* 1.5A Rp */
#define PD_SRC_VNC            PD_SRC_1_5_VNC_MV
#define PD_SRC_RD_THRESHOLD   PD_SRC_1_5_RD_THRESH_MV

/* start as a sink in case we have no other power supply/battery */
#define PD_DEFAULT_STATE PD_STATE_SNK_DISCONNECTED

/* delay for the voltage transition on the power supply, BQ25x spec is 30ms */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  40000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 20000 /* us */

/* delay to turn on/off vconn */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 10000
#define PD_MAX_POWER_MW       24000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_VOLTAGE_MV     12000

/* The lower the input voltage, the higher the power efficiency. */
#define PD_PREFER_LOW_VOLTAGE

/* Set the charge current limit. */
void board_set_charge_limit(int charge_ma);

/* PP1800 transition GPIO interrupt handler */
void pp1800_on_off_evt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
