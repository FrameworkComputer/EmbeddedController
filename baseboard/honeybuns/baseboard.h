/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Honeybuns baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/* EC Defines */
#define CONFIG_CRC8

/* Flash Lyaout */
/*
 * Flash layout: we redefine the sections offsets and sizes as we will use
 * RO/RW regions of different sizes.
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

#define CONFIG_FLASH_PSTATE
/* Do not use a dedicated PSTATE bank */
#undef CONFIG_FLASH_PSTATE_BANK

#define CONFIG_SHAREDLIB_SIZE 0

#define CONFIG_RO_MEM_OFF 0
#define CONFIG_RO_STORAGE_OFF 0
#define CONFIG_RO_SIZE (64 * 1024)

#define CONFIG_RW_MEM_OFF (CONFIG_RO_SIZE + CONFIG_RO_MEM_OFF)
#define CONFIG_RW_STORAGE_OFF 0
#define CONFIG_RW_SIZE \
	(CONFIG_FLASH_SIZE_BYTES - (CONFIG_RW_MEM_OFF - CONFIG_RO_MEM_OFF))

#define CONFIG_EC_PROTECTED_STORAGE_OFF CONFIG_RO_MEM_OFF
#define CONFIG_EC_PROTECTED_STORAGE_SIZE CONFIG_RO_SIZE
#define CONFIG_EC_WRITABLE_STORAGE_OFF CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_SIZE CONFIG_RW_SIZE

#define CONFIG_WP_STORAGE_OFF CONFIG_EC_PROTECTED_STORAGE_OFF
#define CONFIG_WP_STORAGE_SIZE CONFIG_EC_PROTECTED_STORAGE_SIZE

/* 48 MHz SYSCLK clock frequency */
#define CPU_CLOCK 48000000
#define TIM_CLOCK32 2
#define TIM_CLOCK_MSB 3
#define TIM_CLOCK_LSB 15
#define TIM_WATCHDOG 7

/* Honeybuns platform does not have a lid switch */
#undef CONFIG_LID_SWITCH

/* USART and EC console configs */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 3
#define CONFIG_UART_TX_DMA
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 2048
#define CONFIG_UART_TX_DMA_CH STM32_DMAC_USART3_TX
#define CONFIG_UART_TX_DMA_PH DMAMUX_REQ_USART3_TX

/* CBI Configs */
#define I2C_ADDR_EEPROM_FLAGS 0x50
#define CONFIG_CBI_EEPROM
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_CMD_CBI
#define CONFIG_EEPROM_CBI_WP
#define CONFIG_BYPASS_CBI_EEPROM_WP_CHECK
#define GPIO_EC_CBI_WP GPIO_EC_FLASH_WP_ODL
#define CBI_FW_MF_MASK BIT(0)
#define CBI_FW_MF_PREFERENCE(val) (val & (CBI_FW_MF_MASK))

/* USB Configuration */
#define CONFIG_USB
#define CONFIG_STREAM_USB
#define CONFIG_USB_UPDATE
#define CONFIG_USB_SERIALNO
#define DEFAULT_SERIALNO "Uninitialized"
#define CONFIG_MAC_ADDR
#define DEFAULT_MAC_ADDR "Uninitialized"
#define CONFIG_USB_MS_EXTENDED_COMPAT_ID_DESCRIPTOR

/* USB endpoint indexes (use define rather than enum to expand them) */
#define USB_EP_CONTROL 0
#define USB_EP_UPDATE 1
#define USB_EP_COUNT 2

#define USB_IFACE_UPDATE 0
#define USB_IFACE_COUNT 1

#ifndef __ASSEMBLER__
/* USB string indexes */
enum usb_strings {
	USB_STR_DESC = 0,
	USB_STR_VENDOR,
	USB_STR_PRODUCT,
	USB_STR_SERIALNO,
	USB_STR_VERSION,
	USB_STR_UPDATE_NAME,
	USB_STR_COUNT
};
#endif

/* RW Specific Config Options */
#ifdef SECTION_IS_RW
/* No AP on any honeybuns variants */
#undef CONFIG_USB_PD_HOST_CMD

/* USB Type C and USB PD defines */
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV2
#define CONFIG_USB_DRP_ACC_TRYSRC
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CUSTOM_PDO
#define CONFIG_USB_PD_ALT_MODE_UFP_DP
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_REV30
/*
 * Source current limit pull options. Honeybuns always wants TYPEC_RP_3A0
 * current limits for the usbc host port (C0). For port C1, some variants are
 * designed with a 1.5A current limit. This variation is handled via
 * BOARD_C1_1A5_LIMIT which would be set in a variant's board.h file.
 *
 * CONFIG_USB_PD_3A_PORTS should be left at 0 as this will disable DPM from
 * doing any dynamic current limit management.
 */
#undef CONFIG_USB_PD_PULLUP
#define CONFIG_USB_PD_PULLUP TYPEC_RP_3A0
#define CONFIG_USB_PD_3A_PORTS 0
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_PS8805
#define CONFIG_USB_PD_TCPM_STM32GX
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_DECODE_SOP
#define CONFIG_USB_PD_VBUS_DETECT_PPC
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USBC_PPC_SN5S330
#define CONFIG_USBC_PPC_VCONN
#define CONFIG_USBC_PPC_DEDICATED_INT
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP
#define CONFIG_USBC_SS_MUX

#define CONFIG_HAS_TASK_PD_INT
#define CONFIG_STM32G4_UCPD_DEBUG
#define CONFIG_CMD_PPC_DUMP
#define CONFIG_CMD_TCPC_DUMP

#define CONFIG_MP4245
#define USB_HUB_OCP_RESET_MSEC (10 * MSEC)

#else /* RO Specific Config Options */

/* RWSIG Config Options */
/* Sign and switch to RW partition on boot. */
#define CONFIG_RWSIG
#define CONFIG_RSA
#define CONFIG_SHA256_UNROLLED
#undef CONFIG_RWSIG_JUMP_TIMEOUT
#define CONFIG_RWSIG_JUMP_TIMEOUT (7000 * MSEC)

/* Don't build PD console command for RO */
#undef CONFIG_CMD_PD
#undef CONFIG_USB_PD_CONSOLE_CMD
#undef CONFIG_USB_PD_HOST_CMD
/* Make sure these files aren't built in RO */
#undef CONFIG_USB_PRL_SM
#undef CONFIG_USB_TYPEC_SM
#undef CONFIG_USB_PE_SM

#endif /* SECTION_IS_RW */

#define CONFIG_RWSIG_TYPE_RWSIG
#define CONFIG_RSA_KEY_SIZE 3072
#define CONFIG_RSA_EXPONENT_3
#define CONFIG_SHA256_SW

/* Define typical operating power and max power. */
#define PD_MAX_VOLTAGE_MV 5000
#define PD_MAX_CURRENT_MA 3000
#define PD_MAX_POWER_MW 15000
#define PD_OPERATING_POWER_MW 15000

/* TODO(b:147314141): Verify these timings */
#define PD_POWER_SUPPLY_TURN_ON_DELAY 30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* I2C Bus Configuration */
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_ENTERING_RW GPIO_EC_ENTERING_RW
#define GPIO_WP_L GPIO_EC_WP_L

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "stddef.h"

struct power_seq {
	enum gpio_signal signal; /* power/reset gpio_signal to control */
	int level; /* level to set in power sequence */
	unsigned int delay_ms; /* delay (in msec) after setting gpio_signal */
};

enum mf_preference {
	MF_OFF = 0,
	MF_ON,
};

/*
 * This is required as adc_channel is included in adc.h which ends up being
 * included when TCPMv2 functions are included
 */
enum adc_channel { ADC_CH_COUNT };

extern const struct power_seq board_power_seq[];
extern const size_t board_power_seq_count;

void baseboard_power_button_evt(int level);

/*
 * Configure the host port to present Rd on both CC lines. This function is
 * called in RO which does not otherwise have usbc/usb-pd support.
 *
 * @return true - initialized. false - not.
 */
int baseboard_usbc_init(int port);

/*
 * Get a board's desired multi-function (MF) prefernce. This allows for board
 * specific policy.
 *
 * @return 1 if multi function (DP + USB3) is preferred, 0 otherwise
 */
int dock_get_mf_preference(void);

/*
 * Initialize and configure PPC used for USB3 only port
 *
 * @return EC success if PPC initialization is successful
 */
int baseboard_config_usbc_usb3_ppc(void);

/*
 * Called from interrupt handler for PS8803 attached.src gpio. This gpio signal
 * will be set high by the PS8803 when it's in the attached.src state and low
 * otherwise. For boards wich have a PPC on this port, this signal is used to
 * enable/disable VBUS in the PPC.
 */
void baseboard_usb3_check_state(void);

/*
 * Set MST_LANE_CONTROL gpio to match the DP pin configuration selected
 * by the host in the DP Configure SVDM message.
 *
 * @param dock_mf 1 -> 2 lanes DP, 0 -> 4 lanes DP
 */
void baseboard_set_mst_lane_control(int dock_mf);

/*
 * Control enable/disable for interrupts used for usb3 only usbc port.
 *
 * @param enable -> 1 for enable, 0 for disable
 */
void baseboard_usbc_usb3_enable_interrupts(int enable);

/*
 * Called from interrupt handler for PPC used on usb3 only port.
 *
 */
void baseboard_usbc_usb3_irq(void);

/**
 * Determine if VBUS is present or not.
 *
 * @param port: The Type-C port number.
 * @return 1 if VBUS is present, 0 if not.
 */
int c1_ps8805_is_vbus_present(int port);

/**
 * Is the port sourcing Vbus?
 *
 * @param port: The Type-C port number.
 * @return 1 if sourcing Vbus, 0 if not.
 */
int c1_ps8805_is_sourcing_vbus(int port);

/**
 * Turn on/off VBUS for port C1
 *
 * @param port: The Type-C port number.
 * @param enable: 1: Turn on VBUS, 0: turn off VBUS.
 * @return EC_SUCCESS on success, error otherwise.
 */
int c1_ps8805_vbus_source_enable(int port, int enable);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
