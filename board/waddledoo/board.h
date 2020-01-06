/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledoo board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_DEDEDE_EC_NPCX796FC
#include "baseboard.h"

/*
 * Remapping of schematic GPIO names to common GPIO names expected (hardcoded)
 * in the EC code base.
 */
#define GPIO_EC_INT_L       GPIO_EC_AP_MKBP_INT_L
#define GPIO_ENTERING_RW    GPIO_EC_ENTERING_RW
#define GPIO_PCH_PWRBTN_L   GPIO_EC_AP_PWR_BTN_ODL
#define GPIO_PCH_WAKE_L     GPIO_EC_AP_WAKE_ODL
#define GPIO_POWER_BUTTON_L GPIO_H1_EC_PWR_BTN_ODL
#define GPIO_VOLUME_UP_L    GPIO_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L  GPIO_VOLDN_BTN_ODL
#define GPIO_WP             GPIO_EC_WP_OD

/* I2C configuration */
#define I2C_PORT_EEPROM     NPCX_I2C_PORT7_0
#define I2C_PORT_BATTERY    NPCX_I2C_PORT5_0
#define I2C_PORT_SENSOR     NPCX_I2C_PORT0_0
#define I2C_PORT_USB_C0     NPCX_I2C_PORT1_0
#define I2C_PORT_SUB_USB_C1 NPCX_I2C_PORT2_0

#define I2C_ADDR_EEPROM_FLAGS 0x50 /* 7b address */

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_1,     /* ADC0 */
	ADC_TEMP_SENSOR_2,     /* ADC1 */
	ADC_SUB_ANALOG,	       /* ADC2 */
	ADC_VSNS_PP1050_ST_S,  /* ADC3 */
	ADC_VSNS_PP3300_A,     /* ADC9 */
	ADC_CH_COUNT
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
