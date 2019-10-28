/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Puff board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define CONFIG_POWER_BUTTON
#define CONFIG_LED_COMMON

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096


/* NPCX7 config */
#define NPCX7_PWM1_SEL    0  /* GPIO C2 is not used as PWM1. */
#define NPCX_UART_MODULE2 1  /* GPIO64/65 are used as UART pins. */

/* Internal SPI flash on NPCX796FC is 512 kB */
#define CONFIG_FLASH_SIZE (512 * 1024)
#define CONFIG_SPI_FLASH_REGS
#define CONFIG_SPI_FLASH_W25Q80 /* Internal SPI flash type. */
#define CONFIG_I2C
#define CONFIG_HOSTCMD_ESPI

/* EC Defines */
#define CONFIG_ADC
#define CONFIG_BOARD_VERSION_CBI
#define CONFIG_CRC8
#define CONFIG_CROS_BOARD_INFO
#define CONFIG_DPTF
#define CONFIG_HIBERNATE_PSL
#undef  CONFIG_LID_SWITCH
#define CONFIG_POWER_BUTTON_IGNORE_LID
#define CONFIG_LTO
#define CONFIG_PWM
#define CONFIG_VBOOT_HASH
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

/* Chipset config */
#define CONFIG_CHIPSET_COMETLAKE
#define CONFIG_CHIPSET_HAS_PRE_INIT_CALLBACK
#define CONFIG_CHIPSET_RESET_HOOK
#define CONFIG_CPU_PROCHOT_ACTIVE_LOW

/* Charger */
#define CONFIG_CHARGE_MANAGER

#define CONFIG_CHARGER_MIN_POWER_MW_FOR_POWER_ON 50000

#define CONFIG_CMD_PD_CONTROL
#define CONFIG_EXTPOWER_GPIO
#undef  CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 1000
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_BUTTON_INIT_IDLE
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD 30
#define CONFIG_DELAY_DSW_PWROK_TO_PWRBTN
#define CONFIG_POWER_COMMON
#define CONFIG_POWER_PP5000_CONTROL
#define CONFIG_POWER_S0IX
#define CONFIG_POWER_S0IX_FAILURE_DETECTION
#define CONFIG_POWER_TRACK_HOST_SLEEP_STATE

/* TODO: (b/143501304) Use correct PD delay values */
#define PD_POWER_SUPPLY_TURN_ON_DELAY	30000	/* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY	250000	/* us */
#define PD_VCONN_SWAP_DELAY		5000	/* us */

/* TODO: (b/143501304) Use correct PD power values */
#define PD_OPERATING_POWER_MW	15000
#define PD_MAX_POWER_MW		60000
#define PD_MAX_CURRENT_MA	3000
#define PD_MAX_VOLTAGE_MV	20000

/* USB */
#undef  CONFIG_USB_CHARGER
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_DISCHARGE_TCPC
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_PORT_COUNT 1
#define CONFIG_USB_PD_VBUS_DETECT_GPIO
#define CONFIG_USB_PD_VBUS_DETECT_PPC
#define CONFIG_USB_PD_TCPC_LOW_POWER
#define CONFIG_USB_PD_TCPM_MUX
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TCPM_PS8751
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_POWER_DELIVERY
#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* I2C Bus Configuration */
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define I2C_PORT_SENSOR		NPCX_I2C_PORT0_0
#define I2C_PORT_PPC0		NPCX_I2C_PORT1_0
#define I2C_PORT_TCPC1		NPCX_I2C_PORT2_0
#define I2C_PORT_TCPC0		NPCX_I2C_PORT3_0
#define I2C_PORT_THERMAL	NPCX_I2C_PORT4_1
#define I2C_PORT_POWER		NPCX_I2C_PORT5_0
#define I2C_PORT_EEPROM		NPCX_I2C_PORT7_0
#define I2C_ADDR_EEPROM_FLAGS	0x50

#define PP5000_PGOOD_POWER_SIGNAL_MASK POWER_SIGNAL_MASK(PP5000_A_PGOOD)

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_VBUS,
	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_LED_RED,
	PWM_CH_LED_GREEN,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum temp_sensor_id {   /* TODO: fix this */
	TEMP_SENSOR_1,
	TEMP_SENSOR_COUNT
};

/* Board specific handlers */
void board_reset_pd_mcu(void);
void board_set_tcpc_power_mode(int port, int mode);
void led_alert(int enable);
void led_critical(void);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
