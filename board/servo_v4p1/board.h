/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Servo V4p1 configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Use Link-Time Optimizations to try to reduce the firmware code size */
#define CONFIG_LTO

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000

/* Servo V4.1 Ports:
 *  CHG - port 0
 *  DUT - port 1
 */
#define CHG 0
#define DUT 1

/*
 * IO expanders I2C addresses and ports
 */
#define TCA6416A_PORT 1
#define TCA6416A_ADDR 0x21
#define TCA6424A_PORT 1
#define TCA6424A_ADDR 0x23

/*
 * Flash layout: we redefine the sections offsets and sizes as we want to
 * include a pstate region, and will use RO/RW regions of different sizes.
 * RO has size 92K and usb_updater along with the majority of code is placed
 *    here.
 * RW has size 40K and usb_updater and other relevant code is placed here.
 */
#undef _IMAGE_SIZE
#undef CONFIG_ROLLBACK_OFF
#undef CONFIG_ROLLBACK_SIZE
#undef CONFIG_FLASH_PSTATE
#undef CONFIG_FW_PSTATE_SIZE
#undef CONFIG_FW_PSTATE_OFF
#undef CONFIG_SHAREDLIB_SIZE
#undef CONFIG_RO_MEM_OFF
#undef CONFIG_RO_STORAGE_OFF
#undef CONFIG_RO_SIZE
#undef CONFIG_RW_MEM_OFF
#undef CONFIG_RW_STORAGE_OFF
#undef CONFIG_RW_SIZE
#undef CONFIG_EC_PROTECTED_STORAGE_OFF
#undef CONFIG_EC_PROTECTED_STORAGE_SIZE
#undef CONFIG_EC_WRITABLE_STORAGE_OFF
#undef CONFIG_EC_WRITABLE_STORAGE_SIZE
#undef CONFIG_WP_STORAGE_OFF
#undef CONFIG_WP_STORAGE_SIZE

#define CONFIG_RAM_BANK_SIZE CONFIG_RAM_SIZE

#define CONFIG_FLASH_PSTATE
#define CONFIG_FLASH_PSTATE_BANK

#define CONFIG_SHAREDLIB_SIZE 0

#define CONFIG_RO_MEM_OFF 0
#define CONFIG_RO_STORAGE_OFF 0
#define CONFIG_RO_SIZE (92 * 1024)

#define CONFIG_FW_PSTATE_OFF (CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE)
#define CONFIG_FW_PSTATE_SIZE CONFIG_FLASH_BANK_SIZE

#define CONFIG_RW_MEM_OFF (CONFIG_FW_PSTATE_OFF + CONFIG_FW_PSTATE_SIZE)
#define CONFIG_RW_STORAGE_OFF 0
#define CONFIG_RW_SIZE \
	(CONFIG_FLASH_SIZE_BYTES - (CONFIG_RW_MEM_OFF - CONFIG_RO_MEM_OFF))

#define CONFIG_EC_PROTECTED_STORAGE_OFF CONFIG_RO_MEM_OFF
#define CONFIG_EC_PROTECTED_STORAGE_SIZE CONFIG_RO_SIZE
#define CONFIG_EC_WRITABLE_STORAGE_OFF CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_SIZE CONFIG_RW_SIZE

#define CONFIG_WP_STORAGE_OFF CONFIG_EC_PROTECTED_STORAGE_OFF
#define CONFIG_WP_STORAGE_SIZE CONFIG_EC_PROTECTED_STORAGE_SIZE

/* Enable USART1,3,4 and USB streams */
#define CONFIG_STREAM_USART
#define CONFIG_STREAM_USART3
#define CONFIG_STREAM_USART4
#define CONFIG_STREAM_USB
#define CONFIG_CMD_USART_INFO

/* The UART console is on USART1 (PA9/PA10) */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 1
#undef CONFIG_UART_TX_DMA
#undef CONFIG_UART_RX_DMA

/* Optional features */
#define CONFIG_HW_CRC
#define CONFIG_PVD
/*
 * See 'Programmable voltage detector characteristics' in the
 * STM32F072x8 Datasheet. PVD Threshold 1 corresponds to a
 * falling voltage threshold of min:2.09V, max:2.27V.
 */
#define PVD_THRESHOLD (1)

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_USB_PID 0x520d
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
#define CONFIG_POWERON_CONF
/*
 * In case of servo_v4_p1 we would need
 * 4 separate bitmasks for CC_CONFIG and USB port muxes.
 */
#define CONFIG_POWERON_CONF_LEN 4

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

/* Enable I/O expander */
#ifdef SECTION_IS_RO
#define CONFIG_IO_EXPANDER
#define CONFIG_IO_EXPANDER_SUPPORT_GET_PORT
#define CONFIG_IO_EXPANDER_TCA64XXA
#define CONFIG_IO_EXPANDER_PORT_COUNT 2
#endif

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
#undef CONFIG_CMD_GETTIME
#undef CONFIG_CMD_MEM
#undef CONFIG_CMD_SHMEM
#undef CONFIG_CMD_SYSLOCK
#undef CONFIG_CMD_TIMERINFO
#undef CONFIG_CMD_WAITMS

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

#define CONFIG_USB_PD_PORT_MAX_COUNT 2

#ifdef SECTION_IS_RO
#define CONFIG_USB_HUB_GL3590
#define CONFIG_INA231
#define CONFIG_CHARGE_MANAGER
#undef CONFIG_CHARGE_MANAGER_SAFE_MODE
#define CONFIG_USB_MUX_TUSB1064
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV1
#define CONFIG_CMD_PD
#define CONFIG_USB_PD_CUSTOM_PDO
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DYNAMIC_SRC_CAP
#define CONFIG_USB_PD_INTERNAL_COMP
#define CONFIG_USB_PD_TCPC
#define CONFIG_USB_PD_TCPM_STUB
#undef CONFIG_USB_PD_PULLUP
/* Default pull-up should not be Rp3a0 due to Cr50 */
#define CONFIG_USB_PD_PULLUP TYPEC_RP_USB
#define CONFIG_USB_PD_VBUS_MEASURE_NOT_PRESENT
#define CONFIG_USB_PD_ONLY_FIXED_PDOS
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_UFP_ONLY

/* Don't automatically change roles */
#undef CONFIG_USB_PD_INITIAL_DRP_STATE
#define CONFIG_USB_PD_INITIAL_DRP_STATE PD_DRP_FORCE_SINK

/* Variable-current Rp no connect and Ra attach macros */
#define CC_NC(port, cc, sel) (pd_tcpc_cc_nc(port, cc, sel))
#define CC_RA(port, cc, sel) (pd_tcpc_cc_ra(port, cc, sel))

/*
 * These power-supply timing values are now set towards maximum spec limit,
 * to give the upstream charger the maximum time to respond.
 *
 * Currently tuned with the Apple 96W adapter.
 * TODO: Change to EVENT-based PS_RDY notification (b/214216304)
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY (121 * MSEC)
#define PD_POWER_SUPPLY_TURN_OFF_DELAY (461 * MSEC)

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW 100000
#define PD_MAX_CURRENT_MA 5000
#define PD_MAX_VOLTAGE_MV 20000

/* Add the raw option to the i2c_xfer command */
#define CONFIG_CMD_I2C_XFER_RAW

/* Enable command for managing host hub */
#define CONFIG_CMD_GL3590
#else
#undef CONFIG_CMD_I2C_XFER
#undef CONFIG_USB_POWER_DELIVERY
#endif /* SECTION_IS_RO */

/*
 * If task profiling is enabled then the rx falling edge detection interrupts
 * can't be processed in time and can't support USB PD messaging.
 */
#undef CONFIG_TASK_PROFILING

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

/* Servo V4.1 Board ID mappings */
enum servo_board_id {
	BOARD_ID_UNSET = -1,
	BOARD_ID_REV0 = 0, /* Proto */
	BOARD_ID_REV1 = 1, /* EVT */
	BOARD_ID_REV2 = 2, /* DVT */
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
