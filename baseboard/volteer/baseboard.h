/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer baseboard configuration */

#ifndef __CROS_EC_BASEBOARD_H
#define __CROS_EC_BASEBOARD_H

/* NPCX7 config */
#define NPCX7_PWM1_SEL    1  /* GPIO C2 is used as PWM1. */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */
/* Internal SPI flash on NPCX796FC is 512 kB */
#define CONFIG_FLASH_SIZE (512 * 1024)
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */

/* EC Defines */
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_CRC8
#define CONFIG_CROS_BOARD_INFO
#define CONFIG_HIBERNATE_PSL
#define CONFIG_PWM
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

/* Host communication */
#define CONFIG_HOSTCMD_ESPI
#define CONFIG_HOSTCMD_ESPI_VW_SLP_S4

/* Chipset config */
#define CONFIG_CHIPSET_TIGERLAKE
#define CONFIG_CHIPSET_X86_RSMRST_DELAY
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_S0IX_FAILURE_DETECTION
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE
#define CONFIG_BOARD_HAS_RTC_RESET

/* Common Keyboard Defines */
#define CONFIG_CMD_KEYBOARD
#define CONFIG_KEYBOARD_BOARD_CONFIG
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_KEYBOARD_KEYPAD
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_KEYBOARD_PWRBTN_ASSERTS_KSI2

/* Sensors */

/* Common charger defines */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_INPUT_CURRENT		512
#define CONFIG_CHARGER_ISL9241
#define CONFIG_CHARGER_SENSE_RESISTOR		10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC	10

#define CONFIG_USB_CHARGER
#define CONFIG_BC12_DETECT_PI3USB9201

/* Common battery defines */
#define CONFIG_BATTERY_SMART
#define CONFIG_BATTERY_FUEL_GAUGE
/* TODO: b/143809318 enable cut off */
/* #define CONFIG_BATTERY_CUT_OFF */

/* Common LED defines */
#define CONFIG_LED_COMMON
#define CONFIG_LED_PWM
/* Although there are 2 LEDs, they are both controlled by the same lines. */
#define CONFIG_LED_PWM_COUNT 1

/* USB Type C and USB PD defines */
/* Enable the new USB-C PD stack */
#define CONFIG_USB_SM_FRAMEWORK
#define CONFIG_USB_TYPEC_SM
#define CONFIG_USB_PRL_SM
#define CONFIG_USB_PE_SM
#define CONFIG_USB_TYPEC_DRP_ACC_TRYSRC

#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USB_PD_DISCHARGE_PPC
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT		TYPEC_RP_3A0
#define CONFIG_USB_PD_PORT_MAX_COUNT			1
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TCPM_TUSB422	/* USBC port C0 */
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_PD_VBUS_DETECT_PPC
#define CONFIG_USB_PD_VBUS_MEASURE_NOT_PRESENT

#define CONFIG_USBC_PPC
#define CONFIG_CMD_PPC_DUMP
/* Note - SN5S330 support automatically adds
 * CONFIG_USBC_PPC_POLARITY
 * CONFIG_USBC_PPC_SBU
 * CONFIG_USBC_PPC_VCONN
 */
#define CONFIG_USBC_PPC_SN5S330		/* USBC port C0 */

#define CONFIG_USBC_SS_MUX
#define CONFIG_USB_MUX_VIRTUAL

#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* TODO: b/144165680 - measure and check these values on Volteer */
#define PD_POWER_SUPPLY_TURN_ON_DELAY	30000 /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY	30000 /* us */
#define PD_VCONN_SWAP_DELAY		5000 /* us */

/*
 * SN5S30 PPC supports up to 24V VBUS source and sink, however passive USB-C
 * cables only support up to 60W.
 */
#define PD_OPERATING_POWER_MW	15000
#define PD_MAX_POWER_MW		60000
#define PD_MAX_CURRENT_MA	3000
#define PD_MAX_VOLTAGE_MV	20000


/* I2C Bus Configuration */
#define CONFIG_I2C
#define I2C_PORT_SENSOR		NPCX_I2C_PORT0_0
#define I2C_PORT_USB_C0		NPCX_I2C_PORT1_0
#define I2C_PORT_USB_C1		NPCX_I2C_PORT2_0
#define I2C_PORT_USB_1_MIX	NPCX_I2C_PORT3_0
#define I2C_PORT_POWER		NPCX_I2C_PORT5_0
#define I2C_PORT_EEPROM		NPCX_I2C_PORT7_0

#define I2C_PORT_BATTERY	I2C_PORT_POWER
#define I2C_PORT_CHARGER	I2C_PORT_EEPROM

#define I2C_ADDR_EEPROM_FLAGS	0x50
#define CONFIG_I2C_MASTER


#ifndef __ASSEMBLER__

#include "gpio_signal.h"


enum adc_channel {
	ADC_TEMP_SENSOR_1_CHARGER,
	ADC_TEMP_SENSOR_2_PP3300_REGULATOR,
	ADC_TEMP_SENSOR_3_DDR_SOC,
	ADC_TEMP_SENSOR_4_FAN,
	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_LED1_BLUE = 0,
	PWM_CH_LED2_GREEN,
	PWM_CH_LED3_RED,
	PWM_CH_COUNT
};

enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_COUNT
};

void board_reset_pd_mcu(void);

/* Common definition for the USB PD interrupt handlers. */
void ppc_interrupt(enum gpio_signal signal);
void tcpc_alert_event(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BASEBOARD_H */
