/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

#include <stdbool.h>

/*
 * By default, enable all console messages excepted HC
 */
#define CC_DEFAULT (CC_ALL & ~(BIT(CC_HOSTCMD)))

/* NPCX7 config */
#define NPCX7_PWM1_SEL 1 /* GPIO C2 is used as PWM1. */
#define NPCX_UART_MODULE2 1 /* GPIO64/65 are used as UART pins. */
/* Internal SPI flash on NPCX796FC is 512 kB */
#define CONFIG_FLASH_SIZE_BYTES (512 * 1024)
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/* Allow objects to be linked into a flash resident section */
#define CONFIG_CHIP_INIT_ROM_REGION

/* System safe mode for improved panic debugging */
#define CONFIG_SYSTEM_SAFE_MODE
#define CONFIG_PANIC_ON_WATCHDOG_WARNING
/* Increase watchdog timeout since system will panic on warning */
#undef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 2100

/* EC Defines */
#define CONFIG_LTO
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_CRC8
#define CONFIG_CBI_EEPROM
#define CONFIG_DPTF
#define CONFIG_FPU
#define CONFIG_HIBERNATE_PSL
#define CONFIG_PWM
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1
#define CONFIG_VOLUME_BUTTONS
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_BOARD_RESET_AFTER_POWER_ON

/* Host communication */
#define CONFIG_HOST_INTERFACE_ESPI
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S4
#define CONFIG_HOST_INTERFACE_ESPI_VW_SLP_S5

/* Chipset config */
#define CONFIG_CHIPSET_TIGERLAKE
#define CONFIG_CHIPSET_PP3300_RAIL_FIRST
#define CONFIG_CHIPSET_SLP_S3_L_OVERRIDE
#define CONFIG_CHIPSET_X86_RSMRST_DELAY
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_S4_RESIDENCY
#define CONFIG_POWER_SLEEP_FAILURE_DETECTION
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_BOARD_HAS_RTC_RESET

/* Common Keyboard Defines */
#define CONFIG_CMD_KEYBOARD

#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_KEYPAD
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_PWM_KBLIGHT

/* Sensors */
#define CONFIG_TABLET_MODE
#define CONFIG_GMR_TABLET_MODE

#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO
#define CONFIG_MKBP_INPUT_DEVICES
#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT

/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* FIFO size is in power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 256
/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

/* Sensor console commands */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

/* Thermal features */
#define CONFIG_FANS FAN_CH_COUNT
#define CONFIG_TEMP_SENSOR
#define CONFIG_TEMP_SENSOR_POWER
#define GPIO_TEMP_SENSOR_POWER GPIO_PG_EC_DSW_PWROK
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_30K9_47K_4050B
#define CONFIG_THROTTLE_AP
#define CONFIG_CHIPSET_CAN_THROTTLE

/* Common charger defines */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT 512
#define CONFIG_CHARGER_MIN_INPUT_CURRENT_LIMIT 512

/*
 * Hardware based charge ramp is broken in the ISL9241 (b/169350714).
 */
#define CONFIG_CHARGE_RAMP_SW
#define CONFIG_CHARGER_ISL9241
/* Setting ISL9241 Register Control1 switching frequency to 724kHz. */
#define CONFIG_ISL9241_SWITCHING_FREQ ISL9241_CONTROL1_SWITCHING_FREQ_724KHZ

#define CONFIG_USB_CHARGER
#define CONFIG_BC12_DETECT_PI3USB9201

/*
 * Don't allow the system to boot to S0 when the battery is low and unable to
 * communicate on locked systems (which haven't PD negotiated)
 */
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON_WITH_BATT 15000
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 15001

/* Common battery defines */
#define CONFIG_BATTERY_SMART
#define CONFIG_BATTERY_FUEL_GAUGE
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BATTERY_HW_PRESENT_CUSTOM
#define CONFIG_BATTERY_REVIVE_DISCONNECT
#define CONFIG_HOSTCMD_BATTERY_V2

/* Common LED defines */
#define CONFIG_LED_COMMON

/* EDP back-light control defines */
#define CONFIG_BACKLIGHT_LID
#define GPIO_ENABLE_BACKLIGHT GPIO_EC_EDP_BL_EN

/* USB Type C and USB PD defines */
/* Enable the new USB-C PD stack */
#define CONFIG_USB_PD_TCPMV2
#define CONFIG_USB_DRP_ACC_TRYSRC
#define CONFIG_USB_PD_REV30

/*
 * TODO(b/158572770): TCPMv2: Conserve flash space
 * Add these console commands as flash space permits.
 */
#undef CONFIG_CMD_HCDEBUG
#undef CONFIG_CMD_ACCELS
#undef CONFIG_CMD_ACCEL_INFO
#undef CONFIG_CMD_ACCELSPOOF
#undef CONFIG_CMD_PPC_DUMP

#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_ALT_MODE_UFP
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_TCPC_RUNTIME_CONFIG
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TCPM_RT1715
#define CONFIG_USB_PD_TCPM_TUSB422 /* USBC port C0 */
#define CONFIG_USB_PD_TCPM_PS8815 /* USBC port USB3 DB */
#define CONFIG_USB_PD_TCPM_PS8815_FORCE_DID
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_HOSTCMD_PD_CONTROL /* Needed for TCPC FW update */
#define CONFIG_CMD_USB_PD_PE

/*
 * Because of the CSE Lite, an extra cold AP reset is needed, and older cr50
 * firmware will not be able to detect it because of updated cr50 pin straps.
 * Therefore, the AP will require the EC to reset it so that the proper reset
 * signal will be read and verstage can execute again.
 */
#define CONFIG_CMD_AP_RESET_LOG
#define CONFIG_HOSTCMD_AP_RESET

/*
 * The PS8815 TCPC was found to require a 50ms delay to consistently work
 * with non-PD chargers.  Override the default low-power mode exit delay.
 */
#undef CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE
#define CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE (50 * MSEC)

/* Enable USB3.2 DRD */
#define CONFIG_USB_PD_USB32_DRD

#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_TCPC

#define CONFIG_USB_MUX_RUNTIME_CONFIG

#define CONFIG_USBC_PPC
/* Note - SN5S330 support automatically adds
 * CONFIG_USBC_PPC_POLARITY
 * CONFIG_USBC_PPC_SBU
 * CONFIG_USBC_PPC_VCONN
 */
#define CONFIG_USBC_PPC_DEDICATED_INT

#define CONFIG_USBC_SS_MUX
#define CONFIG_USB_MUX_VIRTUAL

#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* Enabling SOP* communication */
#define CONFIG_CMD_USB_PD_CABLE
#define CONFIG_USB_PD_DECODE_SOP

/* UART COMMAND */
#define CONFIG_CMD_CHARGEN

/*
 * USB ID
 * This is allocated specifically for Volteer
 * http://google3/hardware/standards/usb/
 */
#define CONFIG_USB_PID 0x503E
/* Device version of product. */
#define CONFIG_USB_BCD_DEV 0x0000

/* Retimer */
#define CONFIG_USBC_RETIMER_INTEL_BB
#define CONFIG_USBC_RETIMER_INTEL_BB_RUNTIME_CONFIG
#define CONFIG_USBC_RETIMER_FW_UPDATE

/* Enable volume button command in EC console */
#define CONFIG_CMD_BUTTON

/* Enable volume button in ectool */
#define CONFIG_HOSTCMD_BUTTON

#ifndef __ASSEMBLER__

#include "baseboard_usbc_config.h"
#include "cbi.h"
#include "common.h"
#include "gpio_signal.h"

enum adc_channel {
	ADC_TEMP_SENSOR_1_CHARGER,
	ADC_TEMP_SENSOR_2_PP3300_REGULATOR,
	ADC_TEMP_SENSOR_3_DDR_SOC,
	ADC_TEMP_SENSOR_4_FAN,
	ADC_CH_COUNT
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
	TEMP_SENSOR_1_CHARGER,
	TEMP_SENSOR_2_PP3300_REGULATOR,
	TEMP_SENSOR_3_DDR_SOC,
	TEMP_SENSOR_4_FAN,
	TEMP_SENSOR_COUNT
};

/*
 * Check battery disconnect state.
 * This function will return if battery is initialized or not.
 * @return true - initialized. false - not.
 */
__override_proto bool board_battery_is_initialized(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
