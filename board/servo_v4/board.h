/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Servo V4 configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define CONFIG_LTO

/* Free up flash space */
#ifdef SECTION_IS_RO
#define CONFIG_DEBUG_ASSERT_BRIEF
#undef CONFIG_USB_PD_TCPMV1_DEBUG
#endif

/*
 * Board Versions:
 * Versions are designated by the PCB color and consist of red, blue, and
 * black. Only the black version has pullup resistors to distinguish its board
 * id from previous versions.
 */
#define BOARD_VERSION_BLACK 3

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* Enable USART1,3,4 and USB streams */
#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USART3
#define CONFIG_STREAM_USART4
#define CONFIG_STREAM_USB
#define CONFIG_CMD_USART_INFO

/* Optional features */
#define CONFIG_HW_CRC
#define CONFIG_PVD
/* See 'Programmable voltage detector characteristics' in the STM32F072x8
   Datasheet. PVD Threshold 1 corresponds to a falling voltage threshold of
   min:2.09V, max:2.27V. */
#define PVD_THRESHOLD (1)

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_PID 0x501b
#define CONFIG_USB_CONSOLE
#define CONFIG_USB_UPDATE
#define CONFIG_USB_BCD_DEV 0x0001 /* v 0.01 */

#define CONFIG_USB_PD_IDENTITY_HW_VERS 1
#define CONFIG_USB_PD_IDENTITY_SW_VERS 1

#define CONFIG_USB_SELF_POWERED

#define CONFIG_USB_SERIALNO
#define DEFAULT_SERIALNO "Uninitialized"
#define CONFIG_MAC_ADDR
#define DEFAULT_MAC_ADDR "Uninitialized"

/* USB interface indexes (use define rather than enum to expand them) */
#define USB_IFACE_CONSOLE 0
#define USB_IFACE_EMPTY 1
#define USB_IFACE_I2C 2
#define USB_IFACE_USART3_STREAM 3
#define USB_IFACE_USART4_STREAM 4
#define USB_IFACE_UPDATE 5
#define USB_IFACE_COUNT 6

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL 0
#define USB_EP_CONSOLE 1
#define USB_EP_EMPTY 2
#define USB_EP_I2C 3
#define USB_EP_USART3_STREAM 4
#define USB_EP_USART4_STREAM 5
#define USB_EP_UPDATE 6
#define USB_EP_COUNT 7

/* Enable console recasting of GPIO type. */
#define CONFIG_CMD_GPIO_EXTENDED

/* This is not actually an EC so disable some features. */
#undef CONFIG_WATCHDOG_HELP
#undef CONFIG_LID_SWITCH
#undef CONFIG_HIBERNATE

/* Remove console commands / features for flash / RAM savings */
#undef CONFIG_USB_PD_HOST_CMD
#undef CONFIG_CONSOLE_CMDHELP
#undef CONFIG_CONSOLE_HISTORY
#undef CONFIG_CMD_CRASH
#undef CONFIG_CMD_CRASH_NESTED
#undef CONFIG_CMD_ACCELSPOOF
#undef CONFIG_CMD_FASTCHARGE
#undef CONFIG_CMD_FLASHINFO
#undef CONFIG_CMD_FLASH_WP
#undef CONFIG_CMD_GETTIME
#undef CONFIG_CMD_I2C_SCAN
#undef CONFIG_CMD_I2C_XFER
#undef CONFIG_CMD_MEM
#undef CONFIG_CMD_SHMEM
#undef CONFIG_CMD_SYSLOCK
#undef CONFIG_CMD_TIMERINFO
#undef CONFIG_CMD_WAITMS
#undef CONFIG_CMD_USART_INFO
#undef CONFIG_CMD_CHARGE_SUPPLIER_INFO
#define CONFIG_CMD_PD_SRCCAPS_REDUCED_SIZE

/* Enable control of I2C over USB */
#define CONFIG_USB_I2C
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER
#define I2C_PORT_MASTER 1

/* PD features */
#define CONFIG_ADC
#undef CONFIG_ADC_WATCHDOG
#define CONFIG_BOARD_PRE_INIT
/*
 * If task profiling is enabled then the rx falling edge detection interrupts
 * can't be processed in time and can't support USB PD messaging.
 */
#undef CONFIG_TASK_PROFILING

#define CONFIG_CHARGE_MANAGER
#undef CONFIG_CHARGE_MANAGER_SAFE_MODE
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV1
#define CONFIG_CMD_PD
#define CONFIG_USB_PD_CUSTOM_PDO
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DYNAMIC_SRC_CAP
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define CONFIG_USB_PD_TCPC
#define CONFIG_USB_PD_TCPM_STUB
#undef CONFIG_USB_PD_PULLUP
#define CONFIG_USB_PD_PULLUP TYPEC_RP_USB
#define CONFIG_USB_PD_VBUS_MEASURE_NOT_PRESENT
#define CONFIG_USB_PD_ONLY_FIXED_PDOS

/* Don't automatically change roles */
#undef CONFIG_USB_PD_INITIAL_DRP_STATE
#define CONFIG_USB_PD_INITIAL_DRP_STATE PD_DRP_FORCE_SINK

/* Variable-current Rp no connect and Ra attach macros */
#define CC_NC(port, cc, sel) (pd_tcpc_cc_nc(port, cc, sel))
#define CC_RA(port, cc, sel) (pd_tcpc_cc_ra(port, cc, sel))

/*
 * TODO(crosbug.com/p/60792): The delay values are currently just place holders
 * and the delay will need to be relative to the circuitry that allows VBUS to
 * be supplied to the DUT port from the CHG port.
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 50000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 50000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 60000
#define PD_MAX_CURRENT_MA 3000
#define PD_MAX_VOLTAGE_MV 20000
/*
 * Define PDO selection logic for SourceCap.
 * On a 45W PD charger, it might provide PDOs with 15V/3A and 20V/2.25A.
 * In this case, pd_find_pdo_index() would always prefer 15V/3A rather than
 * 20V/2.25A and such that the 20V PDO will be disappeared when servo-v4
 * advertise the SrcCap. We define PD_PREFER_HIGH_VOLTAGE so that all the
 * PDOs could be advertised by servo-v4.
 */
#define PD_PREFER_HIGH_VOLTAGE

/*
 * Allow dangerous commands all the time, since we don't have a write protect
 * switch.
 */
#define CONFIG_SYSTEM_UNLOCKED

#ifndef __ASSEMBLER__

/* Timer selection */
#define TIM_CLOCK32 2
#define TIM_ADC 3

#include "gpio_signal.h"

/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_SERIALNO,
	USB_STR_VERSION,
	USB_STR_I2C_NAME,
	USB_STR_CONSOLE_NAME,
	USB_STR_USART3_STREAM_NAME,
	USB_STR_USART4_STREAM_NAME,
	USB_STR_UPDATE_NAME,
	USB_STR_COUNT
};

/* ADC signal */
enum adc_channel {
	ADC_CHG_CC1_PD,
	ADC_CHG_CC2_PD,
	ADC_DUT_CC1_PD,
	ADC_DUT_CC2_PD,
	ADC_SBU1_DET,
	ADC_SBU2_DET,
	ADC_SUB_C_REF,
	/* Number of ADC channels */
	ADC_CH_COUNT
};

/**
 * Compare cc_voltage to disconnect threshold
 *
 * This function can be used for boards that support variable Rp settings and
 * require a different voltage threshold based on the Rp value attached to a
 * given cc line.
 *
 * @param port USB-C port number
 * @param cc_volt voltage measured in mV of the CC line
 * @param cc_sel cc1 or cc2 selection
 * @return 1 if voltage is >= threshold value for disconnect
 */
int pd_tcpc_cc_nc(int port, int cc_volt, int cc_sel);

/**
 * Compare cc_voltage to Ra threshold
 *
 * This function can be used for boards that support variable Rp settings and
 * require a different voltage threshold based on the Rp value attached to a
 * given cc line.
 *
 * @param port USB-C port number
 * @param cc_volt voltage measured in mV of the CC line
 * @param cc_sel cc1 or cc2 selection
 * @return 1 if voltage is < threshold value for Ra attach
 */
int pd_tcpc_cc_ra(int port, int cc_volt, int cc_sel);

/**
 * Set Rp or Rd resistor for CC lines
 *
 * This function is used to configure the CC pullup or pulldown resistor to
 * the requested value.
 *
 * @param port USB-C port number
 * @param cc_pull 1 for Rp and 0 for Rd
 * @param rp_value If cc_pull == 1, the value of Rp to use
 * @return 1 if cc_pull == 1 and Rp is invalid, otherwise 0
 */
int pd_set_rp_rd(int port, int cc_pull, int rp_value);

/**
 * Get board HW ID version
 *
 * @return HW ID version
 */
int board_get_version(void);

/**
 * Enable or disable external HPD detection
 *
 * @param enable Enable external HPD detection if true, otherwise disable
 */
void ext_hpd_detection_enable(int enable);

/**
 * Enable or disable CCD
 *
 * @param enable Enable CCD if true, otherwise disable
 */
void ccd_enable(int enable);
#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
