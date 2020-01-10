/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledoo board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#define VARIANT_DEDEDE_EC_NPCX796FC
#include "baseboard.h"

/* I2C configuration */
#define I2C_PORT_EEPROM     NPCX_I2C_PORT7_0
#define I2C_PORT_BATTERY    NPCX_I2C_PORT5_0
#define I2C_PORT_SENSOR     NPCX_I2C_PORT0_0
#define I2C_PORT_USB_C0     NPCX_I2C_PORT1_0
#define I2C_PORT_SUB_USB_C1 NPCX_I2C_PORT2_0

#define I2C_PORT_ACCEL      I2C_PORT_SENSOR

#define I2C_ADDR_EEPROM_FLAGS 0x50 /* 7b address */

/* Sensors */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

#define CONFIG_ACCEL_BMA255		/* Lid accel */
#define CONFIG_ACCELGYRO_BMI160		/* Base accel */
#define CONFIG_SYNC			/* Camera VSYNC */

/* Lid operates in forced mode, base in FIFO */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)
#define CONFIG_ACCEL_FIFO
#define CONFIG_ACCEL_FIFO_SIZE 256	/* Must be a power of 2 */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_SYNC_INT_EVENT TASK_EVENT_MOTION_SENSOR_INTERRUPT(VSYNC)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define CONFIG_GMR_TABLET_MODE

#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_GPIO

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

enum sensor_id {
	LID_ACCEL,
	BASE_ACCEL,
	BASE_GYRO,
	VSYNC,
	SENSOR_COUNT
};

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
