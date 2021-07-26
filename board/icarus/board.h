/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Configuration for Kukui */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_KUKUI_JACUZZI
#define VARIANT_KUKUI_BATTERY_SMART
#define VARIANT_KUKUI_CHARGER_ISL9238
#define VARIANT_KUKUI_EC_IT81202

#include "baseboard.h"

/* TODO: remove me once we fix IT83XX_ILM_BLOCK_SIZE out of space issue */
#undef CONFIG_LTO

#undef CONFIG_CHIPSET_POWER_SEQ_VERSION
#define CONFIG_CHIPSET_POWER_SEQ_VERSION 1

#define CONFIG_BATTERY_HW_PRESENT_CUSTOM

#define CONFIG_CHARGER_PSYS

#define CONFIG_CHARGER_RUNTIME_CONFIG

#define CONFIG_BC12_DETECT_PI3USB9201

#define CONFIG_EXTPOWER_GPIO
#undef CONFIG_EXTPOWER_DEBOUNCE_MS
#define CONFIG_EXTPOWER_DEBOUNCE_MS 200

#undef CONFIG_I2C_NACK_RETRY_COUNT
#define CONFIG_I2C_NACK_RETRY_COUNT 10
#define CONFIG_SMBUS_PEC

#define CONFIG_USB_PD_TCPM_ITE_ON_CHIP
#define CONFIG_USB_PD_DISCHARGE_GPIO
#define CONFIG_USB_PD_TCPC_LOW_POWER

#define CONFIG_USB_MUX_IT5205

#undef CONFIG_ACCEL_FIFO
#undef CONFIG_ACCEL_FIFO_SIZE
#undef CONFIG_ACCEL_FIFO_THRES

/* I2C ports */
#define I2C_PORT_BC12               IT83XX_I2C_CH_C
#define I2C_PORT_TCPC0              IT83XX_I2C_CH_C
#define I2C_PORT_USB_MUX            IT83XX_I2C_CH_C
#define I2C_PORT_CHARGER            IT83XX_I2C_CH_A
#define I2C_PORT_SENSORS            IT83XX_I2C_CH_B
#define I2C_PORT_ACCEL              I2C_PORT_SENSORS
#define I2C_PORT_BATTERY            IT83XX_I2C_CH_A
#define I2C_PORT_VIRTUAL_BATTERY    I2C_PORT_BATTERY

#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO

#define CONFIG_LED_ONOFF_STATES

#undef CONFIG_GMR_TABLET_MODE
#undef GMR_TABLET_MODE_GPIO_L
#undef CONFIG_TABLET_MODE
#undef CONFIG_TABLET_MODE_SWITCH

#ifndef __ASSEMBLER__

enum adc_channel {
	/* Real ADC channels begin here */
	ADC_BOARD_ID = 0,
	ADC_EC_SKU_ID,
	ADC_VBUS,
	ADC_CH_COUNT
};

/* power signal definitions */
enum power_signal {
	AP_IN_S3_L,
	PMIC_PWR_GOOD,

	/* Number of signals */
	POWER_SIGNAL_COUNT,
};

/* Motion sensors */
enum sensor_id {
	LID_ACCEL = 0,
	BASE_ACCEL,
	BASE_GYRO,
	SENSOR_COUNT,
};

enum charge_port {
	CHARGE_PORT_USB_C,
};

enum battery_type {
	BATTERY_LGC_AP18C8K,
	BATTERY_MURATA_AP18C4K,
	BATTERY_PANASONIC_AP19B5K_KT00305011,
	BATTERY_LGC_AP19B8K,
	BATTERY_COSMX_AP20CBL,
	BATTERY_SMP_AP18C7K,
	BATTERY_TYPE_COUNT,
};

#include "gpio_signal.h"
#include "registers.h"

/* support factory keyboard test */
#define CONFIG_KEYBOARD_FACTORY_TEST
#define GPIO_KBD_KSO2		GPIO_EC_KSO_02_INV

#ifdef SECTION_IS_RO
/* Interrupt handler for AP jump to BL */
void emmc_ap_jump_to_bl(enum gpio_signal signal);
#endif

void bc12_interrupt(enum gpio_signal signal);
void board_reset_pd_mcu(void);
int board_get_version(void);

/* returns the i2c port number of charger/battery */
int board_get_charger_i2c(void);
int board_get_battery_i2c(void);

/* Motion sensor interrupt */
void motion_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
