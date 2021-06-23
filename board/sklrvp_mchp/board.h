/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Intel SKL RVP3 fly wired to MEC152x EVB board-specific configuration
 * Microchip Evaluation Board (EVB) with
 * MEC15211H 144-pin processor card.
 * SKL RVP3 has Kabylake silicon.
 */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * Use UART2 for EC console
 */
#undef CONFIG_UART_CONSOLE
#define CONFIG_UART_CONSOLE 0

/*
 * Initial board bring-up and prevent power button task from
 * generating event to exit G3 state.
 */
/* #define CONFIG_BRINGUP */

/*
 * Debug on EVB with CONFIG_CHIPSET_DEBUG
 * Keep WDG disabled and JTAG enabled.
 * CONFIG_BOARD_PRE_INIT enables JTAG early
 * #define CONFIG_CHIPSET_DEBUG
 */

#ifdef CONFIG_CHIPSET_DEBUG
#ifndef CONFIG_BOARD_PRE_INIT
#define CONFIG_BOARD_PRE_INIT
#endif
#endif

/*
 * DEBUG: Add CRC32 in last 4 bytes of EC_RO/RW binaries
 * in SPI. LFW will use DMA CRC32 HW to check data integrity.
 * #define CONFIG_MCHP_LFW_DEBUG
 */


/*
 * Override Boot-ROM JTAG mode
 * 0x01 = 4-pin standard JTAG
 * 0x03 = ARM 2-pin SWD + 1-pin SWV
 * 0x05 = ARM 2-pin SWD no SWV. Required if UART2 is used.
 */
/* #define CONFIG_MCHP_JTAG_MODE 0x05 */

/* debug MCHP eSPI */
/* #define CONFIG_ESPI_DEBUG */

/*
 * Enable Trace FIFO Debug port
 * When this is undefined all TRACEn() and tracen()
 * macros are defined as blank.
 * Uncomment this define to enable these messages.
 * Only enable if GPIO's 0171 & 0171 are available therefore
 * define this at the board level.
 */
/* #define CONFIG_MCHP_TFDP */

/*
 * Enable MCHP specific GPIO EC UART commands
 * for debug.
 */
/* #define CONFIG_MEC_GPIO_EC_CMDS */

/*
 * Enable CPRINT in chip eSPI module
 * and EC UART test command.
 */
/* #define CONFIG_MCHP_ESPI_DEBUG */

/*
 * DEBUG
 * Disable ARM Cortex-M4 write buffer so
 * exceptions become synchronous.
 *
 * #define CONFIG_DEBUG_DISABLE_WRITE_BUFFER
 */

/*
 * DEBUG: Configure MEC152x GPIO060 as 48MHZ_OUT to
 * verify & debug clock is shutdown in heavy sleep.
 */
/* #define CONFIG_MCHP_48MHZ_OUT */

/*
 * EVB eSPI test mode (no eSPI master connected)
 * #define EVB_NO_ESPI_TEST_MODE
 */

/*
 * Enable board specific ISR on ALL_SYS_PWRGD signal.
 * Requires for handling Kabylake/Skylake RVP3 board's
 * ALL_SYS_PWRGD signal.
 */
#define CONFIG_BOARD_EC_HANDLES_ALL_SYS_PWRGD

/*
 * Maximum clock frequence eSPI EC advertises
 * Values in MHz are 20, 25, 33, 50, and 66
 */
/* SKL/KBL + EVB fly-wire hook up only supports 20MHz */
#define CONFIG_HOSTCMD_ESPI_EC_MAX_FREQ	MCHP_ESPI_CAP1_MAX_FREQ_20M


/*
 * EC eSPI advertises IO lanes
 * 0 = Single
 * 1 = Single and Dual
 * 2 = Single and Quad
 * 3 = Single, Dual, and Quad
 */
/* KBL + EVB fly-wire hook up only support Single mode */
#define CONFIG_HOSTCMD_ESPI_EC_MODE	MCHP_ESPI_CAP1_SINGLE_MODE

/*
 * Bit map of eSPI channels EC advertises
 * bit[0] = 1 Peripheral channel
 * bit[1] = 1 Virtual Wire channel
 * bit[2] = 1 OOB channel
 * bit[3] = 1 Flash channel
 */
#define CONFIG_HOSTCMD_ESPI_EC_CHAN_BITMAP	MCHP_ESPI_CAP0_ALL_CHAN_SUPP

/* MCHP EC variant */
#define VARIANT_INTELRVP_EC_MCHP

/* MECC config */
#define CONFIG_INTEL_RVP_MECC_VERSION_0_9

/* USB MUX */
#define CONFIG_USB_MUX_VIRTUAL

/* Not using EC FAN control */
#undef CONFIG_FANS
#define BOARD_FAN_MIN_RPM 3000
#define BOARD_FAN_MAX_RPM 10000

/* Temperature sensor module has dependency on EC fan control */
#undef CONFIG_TEMP_SENSOR

#include "baseboard.h"

/*
 * Configuration after inclusion of base board.
 * baseboard enables CONFIG_LOW_POWER_IDLE.
 * Disable here.
 * #undef CONFIG_LOW_POWER_IDLE
 */

#define CONFIG_CHIPSET_SKYLAKE
#define GPIO_PG_EC_RSMRST_ODL		GPIO_RSMRST_L_PGOOD
#define GPIO_PCH_DSW_PWROK		GPIO_EC_PCH_DSW_PWROK
#define GPIO_PG_EC_ALL_SYS_PWRGD	GPIO_ALL_SYS_PWRGD
#define CONFIG_BATTERY_PRESENT_GPIO	GPIO_EC_BATT_PRES_L
#define GMR_TABLET_MODE_GPIO_L		GPIO_TABLET_MODE_L
#define GPIO_BAT_LED_RED_L		GPIO_BAT_LED_GREEN_L
#define GPIO_PWR_LED_WHITE_L		GPIO_AC_LED_GREEN_L
/* MEC152x EVB pin used to enable SKL fan(s) */
#define GPIO_FAN_POWER_EN		GPIO_EC_FAN1_PWM


/* Charger */
#define CONFIG_CHARGER_ISL9241

/* DC Jack charge ports */
#undef  CONFIG_DEDICATED_CHARGE_PORT_COUNT
#define CONFIG_DEDICATED_CHARGE_PORT_COUNT 1

/* USB ports */
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#define DEDICATED_CHARGE_PORT 2

#ifdef CONFIG_USBC_VCONN
	#define CONFIG_USBC_VCONN_SWAP
	/* delay to turn on/off vconn */
	#define PD_VCONN_SWAP_DELAY 5000 /* us */
#endif

/*
 * USB PD configuration using FUSB307 chip on I2C as
 * an example.
 */
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
#define CONFIG_USB_PD_TCPM_FUSB307
#define CONFIG_USB_PD_TCPM_TCPCI

#define I2C_PORT_CHARGER		MCHP_I2C_PORT2
#define I2C_PORT_BATTERY		MCHP_I2C_PORT2
#define I2C_PORT_PCA9555_BOARD_ID_GPIO	MCHP_I2C_PORT2
#define I2C_PORT_PORT80			MCHP_I2C_PORT2
#define I2C_ADDR_PCA9555_BOARD_ID_GPIO	0x22
#define PORT80_I2C_ADDR			MAX695X_I2C_ADDR1_FLAGS

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
 *
 *
#define CONFIG_CLOCK_SRC_EXTERNAL
*/
#undef	CONFIG_CLOCK_SRC_EXTERNAL
#define	CONFIG_CLOCK_CRYSTAL

/*
 * MEC1521H loads firmware using QMSPI controller
 * CONFIG_SPI_FLASH_PORT is the index into
 * spi_devices[] in board.c
 */
#define CONFIG_SPI_FLASH_PORT 0
#define CONFIG_SPI_FLASH
/*
 * Google uses smaller flashes on chromebook boards
 * MCHP SPI test dongle for EVB uses 16MB W25Q128F
 * Configure for smaller flash is OK for testing except
 * for SPI flash lock bit.
 */
 #define CONFIG_FLASH_SIZE_BYTES 524288
 #define CONFIG_SPI_FLASH_W25X40

/*
 * Enable extra SPI flash and generic SPI
 * commands via EC UART
 */
#define CONFIG_CMD_SPI_FLASH
#define CONFIG_CMD_SPI_XFER


/* MEC152x does not have GP-SPI controllers */
#undef CONFIG_MCHP_GPSPI


#ifndef __ASSEMBLER__

/* #include "gpio_signal.h" */
/* #include "registers.h" */


enum sklrvp_charge_ports {
	TYPE_C_PORT_0,
	TYPE_C_PORT_1,
};

enum sklrvp_i2c_channel {
	I2C_CHAN_BATT_CHG,
	I2C_CHAN_MISC,
	I2C_CHAN_TCPC_0,
	I2C_CHAN_TCPC_1,
	I2C_CHAN_COUNT,
};

enum battery_type {
	BATTERY_SIMPLO_SMP_HHP_408,
	BATTERY_SIMPLO_SMP_CA_445,
	BATTERY_TYPE_COUNT,
};

/* Map I2C port to controller */
int board_i2c_p2c(int port);

/* Reset PD MCU */
void board_reset_pd_mcu(void);

#ifdef CONFIG_LOW_POWER_IDLE
void board_prepare_for_deep_sleep(void);
void board_resume_from_deep_sleep(void);
#endif

#ifdef CONFIG_BOARD_EC_HANDLES_ALL_SYS_PWRGD
void board_all_sys_pwrgd_interrupt(enum gpio_signal signal);
#endif

int board_get_version(void);

/* MCHP we need to define this for use by baseboard */
#define PD_MAX_POWER_MW 60000

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
