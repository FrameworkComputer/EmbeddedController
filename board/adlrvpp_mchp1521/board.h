/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADLRVP-P-DDR4-MEC1521 board-specific configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Microchip EC variant */
#define VARIANT_INTELRVP_EC_MCHP

/* UART for EC console */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 2

#include "adlrvp.h"

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
/* Power sequencing */
#define GPIO_EC_SPI_OE_N		GPIO_EC_PCH_SPI_OE_N
#define GPIO_PG_EC_ALL_SYS_PWRGD	GPIO_ALL_SYS_PWRGD
#define GPIO_PG_EC_RSMRST_ODL		GPIO_RSMRST_PWRGD_EC_N
#define GPIO_PCH_SLP_S0_L		GPIO_PM_SLP_S0_R_N
#define GPIO_PG_EC_DSW_PWROK		GPIO_EC_TRACE_DATA_2
#define GPIO_VCCST_PWRGD		GPIO_EC_TRACE_DATA_3
#define GPIO_SLP_SUS_L			GPIO_PM_SLP_SUS_N
#define GPIO_SYS_RESET_L		GPIO_DG2_PRESENT
#define GPIO_PCH_RSMRST_L		GPIO_PM_RSMRST_R
#define GPIO_PCH_PWRBTN_L		GPIO_PM_PWRBTN_N_R
#define GPIO_EN_PP3300_A		GPIO_EC_DS3_R
#define GPIO_SYS_PWROK_EC		GPIO_SYS_PWROK_EC_R
#define GPIO_PCH_DSW_PWROK		GPIO_EC_TRACE_DATA_1

/* Buttons */
#define GPIO_LID_OPEN			GPIO_SMC_LID
#define GPIO_VOLUME_UP_L		GPIO_VOL_UP_EC
#define GPIO_VOLUME_DOWN_L		GPIO_VOL_DOWN_EC
#define GPIO_POWER_BUTTON_L		GPIO_PWRBTN_EC_IN_N

/* Sensors */
#define GMR_TABLET_MODE_GPIO_L		GPIO_EC_SLATEMODE_HALLOUT_SNSR_R
#define GPIO_CPU_PROCHOT		GPIO_PROCHOT_EC_R

/* AC & Battery */
#define GPIO_DC_JACK_PRESENT		GPIO_STD_ADP_PRSNT_EC
#define GPIO_AC_PRESENT			GPIO_BC_ACOK_EC_IN
#define CONFIG_BATTERY_PRESENT_GPIO	GPIO_BATT_ID_R

/* eSPI/Host communication */
#define GPIO_ESPI_RESET_L		GPIO_ESPI_RST_EC_R_N
#define GPIO_PCH_WAKE_L			GPIO_SMC_WAKE_SCI_N
#define GPIO_EC_INT_L			GPIO_EC_TRACE_DATA_0

/* H1 */
#define GPIO_WP_L			GPIO_EC_WAKE_CLK_R
#define GPIO_PACKET_MODE_EN		GPIO_EC_TRACE_CLK
#define GPIO_ENTERING_RW		GPIO_DNX_FORCE_RELOAD_EC_R

/* FAN */
#define GPIO_FAN_POWER_EN		GPIO_FAN_PWR_DISABLE

/* LEDs */
#define GPIO_BAT_LED_RED_L		GPIO_PM_BAT_STATUS_LED2
#define GPIO_PWR_LED_WHITE_L		GPIO_PM_PWRBTN_LED

/* Uart */
#define GPIO_UART2_RX			GPIO_EC_UART_RX

/* Case Closed Debug Mode interrupt */
#define GPIO_CCD_MODE_ODL		GPIO_KBC_NUMLOCK

/* USB-C interrupts */
#define GPIO_USBC_TCPC_ALRT_P0		GPIO_TYPEC_EC_SMBUS_ALERT_0_R
#define GPIO_USBC_TCPC_ALRT_P1		GPIO_TYPEC_EC_SMBUS_ALERT_1_R
#define GPIO_USBC_TCPC_PPC_ALRT_P0	GPIO_KBC_SCANOUT_15
#define GPIO_USBC_TCPC_PPC_ALRT_P1	GPIO_KBC_CAPSLOCK


/* I2C ports & Configs */
/* Charger */
#define I2C_PORT_CHARGER		MCHP_I2C_PORT0
/* Port 80 */
#define I2C_PORT_PORT80			MCHP_I2C_PORT0
/* Board ID */
#define I2C_PORT_PCA9555_BOARD_ID_GPIO	MCHP_I2C_PORT0
/* Battery */
#define I2C_PORT_BATTERY		MCHP_I2C_PORT0
/* USB-C I2C */
#define I2C_PORT_TYPEC_0		MCHP_I2C_PORT1
#define I2C_PORT_TYPEC_1		MCHP_I2C_PORT5

/*
 * MEC1521H loads firmware using QMSPI controller
 * CONFIG_SPI_FLASH_PORT is the index into
 * spi_devices[] in board.c
 */
#define CONFIG_SPI_FLASH_PORT 0
#define CONFIG_SPI_FLASH

/*
 * ADLRVP uses 32MB SPI flash- W25R256JVEIQ.But, EC binary to be generated
 * is of size 512KB. This bin is then later appended with 0xFF to become 32MB
 * binary for flashing purpose.
 */
#define CONFIG_FLASH_SIZE_BYTES	(512 * 1024)
#define CONFIG_SPI_FLASH_W25X40	/* TODO: change to W25R256 */

/* ADC channels */
/*
 * Undefining below and redefining based on ADL RVP schematics,
 * as they are already defined with different channels in mchp_ec.h.
 */
#undef ADC_TEMP_SNS_AMBIENT_CHANNEL
#undef ADC_TEMP_SNS_VR_CHANNEL
#undef ADC_TEMP_SNS_DDR_CHANNEL
#undef ADC_TEMP_SNS_SKIN_CHANNEL

#define ADC_TEMP_SNS_AMBIENT_CHANNEL	CHIP_ADC_CH4
#define ADC_TEMP_SNS_VR_CHANNEL		CHIP_ADC_CH5
#define ADC_TEMP_SNS_DDR_CHANNEL	CHIP_ADC_CH6
#define ADC_TEMP_SNS_SKIN_CHANNEL	CHIP_ADC_CH7

/* To do: Remove once fan register details are added in mchp/fan.c */
#undef CONFIG_FANS

/* Use internal silicon 32KHz oscillator */
#undef CONFIG_CLOCK_SRC_EXTERNAL

#ifndef __ASSEMBLER__

enum adlrvp_i2c_channel {
	I2C_CHAN_BATT_CHG,
	I2C_CHAN_TCPC_0,
	I2C_CHAN_TCPC_1,
	I2C_CHAN_COUNT,
};

/* Map I2C port to controller */
int board_i2c_p2c(int port);
#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
