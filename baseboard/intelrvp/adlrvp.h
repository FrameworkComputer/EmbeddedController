/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADL-RVP specific configuration */

#ifndef __ADLRVP_BOARD_H
#define __ADLRVP_BOARD_H

/* Temperature sensor */
#define CONFIG_TEMP_SENSOR

#include "baseboard.h"

/* RVP Board ids */
#define CONFIG_BOARD_VERSION_GPIO
#define ADLM_LP4_RVP1_SKU_BOARD_ID 0x01
#define ADLM_LP5_RVP2_SKU_BOARD_ID 0x02
#define ADLM_LP5_RVP3_SKU_BOARD_ID 0x03
#define ADLN_LP5_ERB_SKU_BOARD_ID 0x06
#define ADLN_LP5_RVP_SKU_BOARD_ID 0x07
#define ADLP_DDR5_RVP_SKU_BOARD_ID 0x12
#define ADLP_LP5_T4_RVP_SKU_BOARD_ID 0x13
#define ADL_RVP_BOARD_ID(id) ((id) & 0x3F)

/* MECC config */
#define CONFIG_INTEL_RVP_MECC_VERSION_1_0

/* Support early firmware selection */
#define CONFIG_VBOOT_EFS2

/* Chipset */
#define CONFIG_CHIPSET_ALDERLAKE

/* ADL has new low-power features that require extra-wide virtual wire
 * pulses. The EDS specifies 100 microseconds. */
#undef CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US
#define CONFIG_HOST_INTERFACE_ESPI_DEFAULT_VW_WIDTH_US 100

/* USB PD config */
#if defined(HAS_TASK_PD_C3)
#define CONFIG_USB_PD_PORT_MAX_COUNT 4
#elif defined(HAS_TASK_PD_C2)
#define CONFIG_USB_PD_PORT_MAX_COUNT 3
#elif defined(HAS_TASK_PD_C1)
#define CONFIG_USB_PD_PORT_MAX_COUNT 2
#else
#define CONFIG_USB_PD_PORT_MAX_COUNT 1
#endif
#define CONFIG_USB_MUX_VIRTUAL
#define CONFIG_USB_MUX_TUSB1044
#define PD_MAX_POWER_MW 100000

#define CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY

/* TCPC AIC config */
/* Support NXP PCA9675 I/O expander. */
#define CONFIG_IO_EXPANDER
#define CONFIG_IO_EXPANDER_PCA9675
#define I2C_ADDR_PCA9675_TCPC_AIC_IOEX 0x21

/* DC Jack charge ports */
#undef CONFIG_DEDICATED_CHARGE_PORT_COUNT
#define CONFIG_DEDICATED_CHARGE_PORT_COUNT 1
#define DEDICATED_CHARGE_PORT CONFIG_USB_PD_PORT_MAX_COUNT

/* PPC */
#define CONFIG_USBC_PPC_SN5S330
#define CONFIG_USB_PD_VBUS_DETECT_PPC
#define CONFIG_USB_PD_DISCHARGE_PPC
#define I2C_ADDR_SN5S330_TCPC_AIC_PPC 0x40

/* TCPC */
#define CONFIG_USB_PD_DISCHARGE
#define CONFIG_USB_PD_TCPM_FUSB302
#define I2C_ADDR_FUSB302_TCPC_AIC 0x22

/* Config BB retimer */
#define CONFIG_USBC_RETIMER_INTEL_BB
#define CONFIG_USBC_RETIMER_FW_UPDATE

/* Connector side BB retimers */
#define I2C_PORT0_BB_RETIMER_ADDR 0x56
#if defined(HAS_TASK_PD_C1)
#define I2C_PORT1_BB_RETIMER_ADDR 0x57
#endif
#if defined(HAS_TASK_PD_C2)
#define I2C_PORT2_BB_RETIMER_ADDR 0x58
#endif
#if defined(HAS_TASK_PD_C3)
#define I2C_PORT3_BB_RETIMER_ADDR 0x59
#endif

/* SOC side BB retimers (dual retimer config) */
#define I2C_PORT0_BB_RETIMER_SOC_ADDR 0x54
#if defined(HAS_TASK_PD_C1)
#define I2C_PORT1_BB_RETIMER_SOC_ADDR 0x55
#endif

/* I2C EEPROM */
#define I2C_ADDR_EEPROM_FLAGS 0x50
#define I2C_PORT_EEPROM I2C_PORT_PCA9555_BOARD_ID_GPIO

/* Enable CBI */
#define CONFIG_CBI_EEPROM

/* Configure mux at runtime */
#define CONFIG_USB_MUX_RUNTIME_CONFIG

/* Enable VCONN */
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* Enabling Thunderbolt-compatible mode */
#define CONFIG_USB_PD_TBT_COMPAT_MODE

/* Enabling USB4 mode */
#define CONFIG_USB_PD_USB4

/* Enable low power mode */
#define CONFIG_USB_PD_TCPC_LOW_POWER

/* Config Fan */
#define CONFIG_FANS 1
#define BOARD_FAN_MIN_RPM 3000
#define BOARD_FAN_MAX_RPM 10000

/* Charger Configs */
#define CONFIG_CHARGER_RUNTIME_CONFIG
/* Charger chip on ADL-P, ADL-M */
#define CONFIG_CHARGER_ISL9241
/* Charger chip on ADL-N */
#define CONFIG_CHARGER_BQ25720
#define CONFIG_CHARGER_BQ25720_VSYS_TH2_CUSTOM
#define CONFIG_CHARGER_BQ25720_VSYS_TH2_DV 70
#define CONFIG_CHARGER_BQ25710_SENSE_RESISTOR 10
#define CONFIG_CHARGER_BQ25710_SENSE_RESISTOR_AC 10

/* Port 80 */
#define PORT80_I2C_ADDR MAX695X_I2C_ADDR1_FLAGS

/* Board Id */
#define I2C_ADDR_PCA9555_BOARD_ID_GPIO 0x22

/*
 * Frequent watchdog timer resets are seen, with the
 * increase in number of type-c ports. So increase
 * the timer value to support more type-c ports.
 */
#ifdef VARIANT_INTELRVP_EC_IT8320
#if defined(HAS_TASK_PD_C2) && defined(HAS_TASK_PD_C3)
#undef CONFIG_WATCHDOG_PERIOD_MS
#define CONFIG_WATCHDOG_PERIOD_MS 4000
#endif
#endif

/*
 * Enable support for battery hostcmd, supporting longer strings.
 * Support for EC_CMD_BATTERY_GET_STATIC version 1.
 */
#define CONFIG_BATTERY_V2
#define CONFIG_BATTERY_COUNT 1
#define CONFIG_HOSTCMD_BATTERY_V2

/* Config to indicate battery type doesn't auto detect */
#define CONFIG_BATTERY_TYPE_NO_AUTO_DETECT

/* Enable system boot time logging */
#define CONFIG_SYSTEM_BOOT_TIME_LOGGING

#ifndef __ASSEMBLER__

enum adlrvp_charge_ports {
	TYPE_C_PORT_0,
#if defined(HAS_TASK_PD_C1)
	TYPE_C_PORT_1,
#endif
#if defined(HAS_TASK_PD_C2)
	TYPE_C_PORT_2,
#endif
#if defined(HAS_TASK_PD_C3)
	TYPE_C_PORT_3,
#endif
};

/*
 * Each Type-C add in card has two I/O expanders hence even if one Type-C port
 * is enabled other I/O expander is available for usage.
 */
enum ioex_port {
	IOEX_C0_PCA9675,
	IOEX_C1_PCA9675,
#if defined(HAS_TASK_PD_C2)
	IOEX_C2_PCA9675,
	IOEX_C3_PCA9675,
#endif
	IOEX_PORT_COUNT
};
#define CONFIG_IO_EXPANDER_PORT_COUNT IOEX_PORT_COUNT

enum battery_type {
	BATTERY_GETAC_SMP_HHP_408_3S,
	BATTERY_GETAC_SMP_HHP_408_2S,
	BATTERY_TYPE_COUNT,
};

/* I2C access in polling mode before task is initialized */
#define CONFIG_I2C_BITBANG

enum adlrvp_bitbang_i2c_channel {
	I2C_BITBANG_CHAN_BRD_ID,
	I2C_BITBANG_CHAN_IOEX_0,
	I2C_BITBANG_CHAN_COUNT
};
#define I2C_BITBANG_PORT_COUNT I2C_BITBANG_CHAN_COUNT

void espi_reset_pin_asserted_interrupt(enum gpio_signal signal);
void extpower_interrupt(enum gpio_signal signal);
void ppc_interrupt(enum gpio_signal signal);
void tcpc_alert_event(enum gpio_signal signal);
void board_connect_c0_sbu(enum gpio_signal s);
int board_get_version(void);
void battery_detect_interrupt(enum gpio_signal signal);
void set_charger_system_voltage(void);

#endif /* !__ASSEMBLER__ */

#endif /* __ADLRVP_BOARD_H */
