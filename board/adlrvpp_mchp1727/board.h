/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADL-P-RVP-MCHP1727 board-specific configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* MCHP EC variant */
#define VARIANT_INTELRVP_EC_MCHP

/* UART for EC console */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 0

#include "adlrvp.h"

/*
 * External parallel crystal between XTAL1 and XTAL2 pins.
 *   #define CONFIG_CLOCK_SRC_EXTERNAL
 *   #define CONFIG_CLOCK_CRYSTAL
 * External single ended 32KHz 50% duty cycle input clock.
 *   #define CONFIG_CLOCK_SRC_EXTERNAL
 *   #undef CONFIG_CLOCK_CRYSTAL
 * Use internal silicon 32KHz oscillator
 *   #undef CONFIG_CLOCK_SRC_EXTERNAL
 *   CONFIG_CLOCK_CRYSTAL is a don't care
 */
#undef	CONFIG_CLOCK_SRC_EXTERNAL

/* MEC1727 integrated SPI chip 512KB SST25PF040C */
#define CONFIG_SPI_FLASH_W25X40

/*
 * Enable extra SPI flash and generic SPI
 * commands via EC UART
 */
#define CONFIG_CMD_SPI_FLASH
#define CONFIG_CMD_SPI_XFER

/* MEC172x does not apply GP-SPI controllers */
#undef CONFIG_MCHP_GPSPI

/* ADC channels */
#undef ADC_TEMP_SNS_AMBIENT_CHANNEL
#undef ADC_TEMP_SNS_DDR_CHANNEL
#undef ADC_TEMP_SNS_SKIN_CHANNEL
#undef ADC_TEMP_SNS_VR_CHANNEL
#define ADC_TEMP_SNS_AMBIENT_CHANNEL	CHIP_ADC_CH3
#define ADC_TEMP_SNS_DDR_CHANNEL	CHIP_ADC_CH5
#define ADC_TEMP_SNS_SKIN_CHANNEL	CHIP_ADC_CH4
#define ADC_TEMP_SNS_VR_CHANNEL		CHIP_ADC_CH0

/*
 * ADC maximum voltage is a board level configuration.
 * MEC172x ADC can use an external 3.0 or 3.3V reference with
 * maximum values up to the reference voltage.
 * The ADC maximum voltage depends upon the external reference
 * voltage connected to MEC172x.
 */
#undef ADC_MAX_MVOLT
#define ADC_MAX_MVOLT 3300

/*
 * Macros for GPIO signals used in common code that don't match the
 * schematic names. Signal names in gpio.inc match the schematic and are
 * then redefined here to so it's more clear which signal is being used for
 * which purpose.
 */
/* Power sequencing */
#define GPIO_EC_SPI_OE_N		GPIO_EC_SPI_OE_MECC
#define GPIO_PG_EC_ALL_SYS_PWRGD	GPIO_ALL_SYS_PWRGD
#define GPIO_PG_EC_RSMRST_ODL		GPIO_RSMRST_PWRGD
#define GPIO_PCH_SLP_S0_L		GPIO_PCH_SLP_S0_N
#define GPIO_PG_EC_DSW_PWROK		GPIO_VCCPDSW_3P3
#define GPIO_SLP_SUS_L			GPIO_PM_SLP_SUS_EC_N
#define GPIO_SYS_RESET_L		GPIO_SYS_RST_ODL
#define GPIO_PCH_RSMRST_L		GPIO_PM_RSMRST_N
#define GPIO_PCH_PWRBTN_L		GPIO_PM_PWRBTN_N
#define GPIO_EN_PP3300_A		GPIO_EC_DS3
#define GPIO_SYS_PWROK_EC		GPIO_SYS_PWROK
#define GPIO_PCH_DSW_PWROK		GPIO_EC_DSW_PWROK

/* Sensors */
#define GMR_TABLET_MODE_GPIO_L		GPIO_SLATE_MODE_INDICATION
#define GPIO_CPU_PROCHOT		GPIO_PROCHOT_EC_N

/* Buttons */
#define GPIO_LID_OPEN			GPIO_SMC_LID
#define GPIO_VOLUME_UP_L		GPIO_VOLUME_UP
#define GPIO_VOLUME_DOWN_L		GPIO_VOL_DN_EC
#define GPIO_POWER_BUTTON_L		GPIO_MECH_PWR_BTN_ODL

/* H1 */
#define GPIO_WP_L			GPIO_EC_FLASH_WP_ODL
#define GPIO_PACKET_MODE_EN		GPIO_EC_H1_PACKET_MODE
#define GPIO_ENTERING_RW		GPIO_EC_ENTERING_RW

/* AC & Battery */
#define GPIO_DC_JACK_PRESENT		GPIO_STD_ADP_PRSNT
#define GPIO_AC_PRESENT			GPIO_BC_ACOK
#define CONFIG_BATTERY_PRESENT_GPIO	GPIO_BAT_DET

/* eSPI/Host communication */
#define GPIO_ESPI_RESET_L		GPIO_LPC_ESPI_RST_N
#define GPIO_PCH_WAKE_L			GPIO_SMC_WAKE_SCI_N_MECC
#define GPIO_EC_INT_L			GPIO_EC_PCH_MKBP_INT_ODL

/* LED */
#define GPIO_BAT_LED_RED_L		GPIO_LED_1_L
#define GPIO_PWR_LED_WHITE_L		GPIO_LED_2_L

/* FAN */
#define GPIO_FAN_POWER_EN		GPIO_THERM_SEN_MECC

/* Charger */
#define I2C_PORT_CHARGER	MCHP_I2C_PORT0

/* Battery */
#define I2C_PORT_BATTERY	MCHP_I2C_PORT0

/* Board ID */
#define I2C_PORT_PCA9555_BOARD_ID_GPIO	MCHP_I2C_PORT0

/* Port 80 */
#define I2C_PORT_PORT80		MCHP_I2C_PORT0

/* USB-C I2C */
#define I2C_PORT_TYPEC_0	MCHP_I2C_PORT6
/*
 * Note: I2C for Type-C Port-1 is swapped with Type-C Port-2
 *       on the RVP to reduce BOM stuffing options.
 */
#define I2C_PORT_TYPEC_1	MCHP_I2C_PORT3
#if defined(HAS_TASK_PD_C2)
#define I2C_PORT_TYPEC_2	MCHP_I2C_PORT7
#define I2C_PORT_TYPEC_3	MCHP_I2C_PORT2
#endif

#ifndef __ASSEMBLER__

enum adlrvp_i2c_channel {
	I2C_CHAN_BATT_CHG,
	I2C_CHAN_TYPEC_0,
	I2C_CHAN_TYPEC_1,
#if defined(HAS_TASK_PD_C2)
	I2C_CHAN_TYPEC_2,
	I2C_CHAN_TYPEC_3,
#endif
	I2C_CHAN_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
