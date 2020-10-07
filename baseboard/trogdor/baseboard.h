/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Trogdor baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/*
 * By default, enable all console messages excepted event and HC:
 * The sensor stack is generating a lot of activity.
 * They can be enabled through the console command 'chan'.
 */
#define CC_DEFAULT     (CC_ALL & ~(CC_MASK(CC_EVENTS) | CC_MASK(CC_HOSTCMD)))

/* NPCX7 config */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
#define NPCX_TACH_SEL2    0  /* No tach. */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is not used as PWM1. */

/* Internal SPI flash on NPCX7 */
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/* EC Modules */
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_I2C_VIRTUAL_BATTERY
#define CONFIG_I2C_PASSTHRU_RESTRICTED
#define CONFIG_LED_COMMON
#define CONFIG_LOW_POWER_IDLE
#define CONFIG_ADC
#define CONFIG_BACKLIGHT_LID
#define CONFIG_FPU
#define CONFIG_PWM
#define CONFIG_PWM_DISPLIGHT

#define CONFIG_VBOOT_HASH

#undef CONFIG_PECI

#define CONFIG_HOSTCMD_SPS
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_HOSTCMD_SECTION_SORTED
#define CONFIG_MKBP_EVENT
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_MKBP_USE_GPIO

#define CONFIG_BOARD_VERSION_GPIO
#define CONFIG_POWER_BUTTON
#define CONFIG_VOLUME_BUTTONS
#define CONFIG_EMULATED_SYSRQ
#define CONFIG_CMD_BUTTON
#define CONFIG_SWITCH
#define CONFIG_LID_SWITCH
#define CONFIG_EXTPOWER_GPIO

/*
 * On power-on, H1 releases the EC from reset but then quickly asserts and
 * releases the reset a second time. This means the EC sees 2 resets:
 * (1) power-on reset, (2) reset-pin reset. This config will
 * allow the second reset to be treated as a power-on.
 */
#define CONFIG_BOARD_RESET_AFTER_POWER_ON

/* Battery */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_GPIO GPIO_BATT_PRES_ODL
#define CONFIG_BATTERY_SMART

/* Charger */
#define CONFIG_CHARGER
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER_ISL9238
#define CONFIG_CHARGE_RAMP_HW
#define CONFIG_USB_CHARGER
#define CONFIG_CMD_CHARGER_ADC_AMON_BMON
#define CONFIG_CHARGER_PSYS
#define CONFIG_CHARGER_PSYS_READ
#define CONFIG_CHARGER_DISCHARGE_ON_AC

#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 2
#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 10000
#define CONFIG_CHARGER_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20

/*
 * USB ID
 *
 * This is allocated specifically for Trogdor
 * http://google3/hardware/standards/usb/
 */
#define CONFIG_USB_PID 0x5043

/* USB */
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_TCPMV2
#define CONFIG_USB_DRP_ACC_TRYSRC
#define CONFIG_USB_PD_DECODE_SOP
#define CONFIG_HOSTCMD_PD_CONTROL
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT TYPEC_RP_3A0
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_CMD_TCPC_DUMP
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_TCPC
#define CONFIG_USB_PD_5V_EN_CUSTOM
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* RTC */
#define CONFIG_CMD_RTC
#define CONFIG_HOSTCMD_RTC

/* Sensors */
/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* FIFO size is a power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 256
/* Depends on how fast the AP boots and typical ODRs. */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

/* PD */
#define PD_POWER_SUPPLY_TURN_ON_DELAY   30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY  250000 /* us */
#define PD_VCONN_SWAP_DELAY             5000 /* us */

#define PD_OPERATING_POWER_MW   10000
#define PD_MAX_POWER_MW         ((PD_MAX_VOLTAGE_MV * PD_MAX_CURRENT_MA) / 1000)
#define PD_MAX_CURRENT_MA       3000
#define PD_MAX_VOLTAGE_MV       20000

/* Chipset */
#define CONFIG_CHIPSET_SC7180
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_CHIPSET_RESUME_INIT_HOOK
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_PP5000_CONTROL
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_POWER_SLEEP_FAILURE_DETECTION

/* NPCX Features */
#define CONFIG_HIBERNATE_PSL

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
#define GPIO_POWER_BUTTON_L	GPIO_EC_PWR_BTN_ODL
#define GPIO_VOLUME_DOWN_L	GPIO_EC_VOLDN_BTN_ODL
#define GPIO_VOLUME_UP_L	GPIO_EC_VOLUP_BTN_ODL
#define GPIO_LID_OPEN		GPIO_LID_OPEN_EC
#define GPIO_SHI_CS_L		GPIO_AP_EC_SPI_CS_L
#define GPIO_ENTERING_RW	GPIO_EC_ENTERING_RW
#define GPIO_BATT_PRES_ODL	GPIO_EC_BATT_PRES_ODL
#define GPIO_EN_PP5000		GPIO_EN_PP5000_A
#define GPIO_ENABLE_BACKLIGHT	GPIO_EC_BL_DISABLE_L
#define GPIO_BOARD_VERSION1	GPIO_BRD_ID0
#define GPIO_BOARD_VERSION2	GPIO_BRD_ID1
#define GPIO_BOARD_VERSION3	GPIO_BRD_ID2
#define GPIO_KBD_KSO2		GPIO_EC_KSO_02_INV

/* I2C Ports */
#define I2C_PORT_BATTERY I2C_PORT_POWER
#define I2C_PORT_VIRTUAL_BATTERY I2C_PORT_BATTERY
#define I2C_PORT_CHARGER I2C_PORT_POWER
#define I2C_PORT_ACCEL   I2C_PORT_SENSOR
#define I2C_PORT_POWER   NPCX_I2C_PORT0_0
#define I2C_PORT_TCPC0   NPCX_I2C_PORT1_0
#define I2C_PORT_TCPC1   NPCX_I2C_PORT2_0
#define I2C_PORT_WLC     NPCX_I2C_PORT3_0
#define I2C_PORT_EEPROM  NPCX_I2C_PORT5_0
#define I2C_PORT_SENSOR  NPCX_I2C_PORT7_0

/* Define the host events which are allowed to wake AP up from S3 */
#define CONFIG_MKBP_HOST_EVENT_WAKEUP_MASK \
		(EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN) | \
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON) | \
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_CONNECTED) | \
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_DISCONNECTED) | \
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_RTC) | \
		 EC_HOST_EVENT_MASK(EC_HOST_EVENT_MODE_CHANGE))

/* And the MKBP events */
#define CONFIG_MKBP_EVENT_WAKEUP_MASK \
		(BIT(EC_MKBP_EVENT_KEY_MATRIX) | \
		 BIT(EC_MKBP_EVENT_HOST_EVENT) | \
		 BIT(EC_MKBP_EVENT_SENSOR_FIFO))

#ifndef __ASSEMBLER__

enum power_signal {
	SC7180_AP_RST_ASSERTED = 0,
	SC7180_PS_HOLD,
	SC7180_PMIC_FAULT_L,
	SC7180_POWER_GOOD,
	SC7180_WARM_RESET,
	SC7180_AP_SUSPEND,
	SC7180_DEPRECATED_AP_RST_REQ,
	/* Number of power signals */
	POWER_SIGNAL_COUNT
};

#endif /* !defined(__ASSEMBLER__) */

#endif /* __CROS_EC_BASEBOARD_H */
