/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* BMI3XX gsensor module for Chrome EC */

#ifndef __CROS_EC_ACCELGYRO_BMI3XX_H
#define __CROS_EC_ACCELGYRO_BMI3XX_H

/* Sensor Specific macros */
#define BMI3_ADDR_I2C_PRIM 0x68
#define BMI3_ADDR_I2C_SEC 0x69
#define BMI3_16_BIT_RESOLUTION 16

/* Chip-specific registers */
#define BMI3_REG_CHIP_ID 0x00
#define BMI3_REG_ERR_REG 0x01

#define BMI3_REG_STATUS 0x02
#define BMI3_STAT_DATA_RDY_ACCEL_POS 7
#define BMI3_STAT_DATA_RDY_ACCEL_MSK 0x80

#define BMI3_REG_ACC_DATA_X 0x03
#define BMI3_REG_ACC_DATA_Y 0x04
#define BMI3_REG_ACC_DATA_Z 0x05
#define BMI3_ACC_RANGE_2G 0x00
#define BMI3_ACC_RANGE_4G 0x01
#define BMI3_ACC_RANGE_8G 0x02
#define BMI3_ACC_RANGE_16G 0x03
#define BMI3_ACC_MODE_DISABLE 0x00
#define BMI3_ACC_MODE_LOW_PWR 0x03
#define BMI3_ACC_MODE_NORMAL 0X04
#define BMI3_ACC_MODE_HIGH_PERF 0x07

#define BMI3_REG_GYR_DATA_X 0x06
#define BMI3_REG_GYR_DATA_Y 0x07
#define BMI3_REG_GYR_DATA_Z 0x08
#define BMI3_GYR_RANGE_125DPS 0x00
#define BMI3_GYR_RANGE_250DPS 0x01
#define BMI3_GYR_RANGE_500DPS 0x02
#define BMI3_GYR_RANGE_1000DPS 0x03
#define BMI3_GYR_RANGE_2000DPS 0x04
#define BMI3_GYR_MODE_DISABLE 0x00
#define BMI3_GYR_MODE_SUSPEND 0X01
#define BMI3_GYR_MODE_ULTRA_LOW_PWR 0X02
#define BMI3_GYR_MODE_LOW_PWR 0x03
#define BMI3_GYR_MODE_NORMAL 0X04
#define BMI3_GYR_MODE_HIGH_PERF 0x07

#define BMI3_REG_TEMP_DATA 0x09

#define BMI3_REG_SENSOR_TIME_0 0x0a
#define BMI3_REG_SENSOR_TIME_1 0x0b

#define BMI3_REG_INT_STATUS_INT1 0x0D
#define BMI3_REG_FIFO_FILL_LVL 0x15
#define BMI3_REG_FIFO_DATA 0x16
#define BMI3_REG_ACC_CONF 0x20
#define BMI3_REG_GYR_CONF 0x21
#define BMI3_REG_INT_MAP1 0x3A
#define BMI3_REG_FIFO_WATERMARK 0x35
#define BMI3_REG_UGAIN_OFF_SEL 0x3F
#define BMI3_REG_FIFO_CONF 0x36
#define BMI3_FIFO_STOP_ON_FULL 0x01
#define BMI3_FIFO_TIME_EN 0x01
#define BMI3_FIFO_ACC_EN 0x02
#define BMI3_FIFO_GYR_EN 0x04
#define BMI3_FIFO_TEMP_EN 0x08
#define BMI3_FIFO_ALL_EN 0x0F

#define BMI3_REG_FIFO_CTRL 0x37
#define BMI3_REG_IO_INT_CTRL 0x38
#define BMI3_INT1_LVL_MASK 0x01
#define BMI3_INT1_OD_MASK 0x02
#define BMI3_INT1_OD_POS 1
#define BMI3_INT1_OUTPUT_EN_MASK 0x04
#define BMI3_INT1_OUTPUT_EN_POS 2
#define BMI3_INT_PUSH_PULL 0
#define BMI3_INT_OPEN_DRAIN 1
#define BMI3_INT_ACTIVE_LOW 0
#define BMI3_INT_ACTIVE_HIGH 1

#define BMI3_REG_IO_INT_CONF 0x39
#define BMI3_INT_LATCH_EN 1
#define BMI3_INT_LATCH_DISABLE 0

#define BMI3_REG_FEATURE_ENGINE_GLOB_CTRL 0x40

#define BMI3_FEATURE_EVENT_EXT 0x47
#define BMI3_PORTRAIT_LANDSCAPE_MASK 0x03
#define BMI3_PORTRAIT 0
#define BMI3_LANDSCAPE 1
#define BMI3_PORTRAIT_INVERT 2
#define BMI3_LANDSCAPE_INVERT 3

#define BMI3_ACC_DP_OFF_X 0x60
#define BMI3_ACC_DP_OFF_Y 0x62
#define BMI3_ACC_DP_OFF_Z 0x64

#define BMI3_GYR_DP_OFF_X 0x66
#define BMI3_GYR_DP_OFF_Y 0x68
#define BMI3_GYR_DP_OFF_Z 0x6A

#define BMI3_REG_CMD 0x7E
#define BMI3_CMD_SOFT_RESET 0xDEAF

/* BMI3 Interrupt Output Enable */
#define BMI3_INT_OUTPUT_DISABLE 0
#define BMI3_INT_OUTPUT_ENABLE 1

/* FIFO sensor data length (in word), Accel or Gyro */
#define BMI3_FIFO_ENTRY 0x3

/* Macro to define accelerometer configuration value for FOC */
#define BMI3_FOC_ACC_CONF_VAL_LSB 0xB7
#define BMI3_FOC_ACC_CONF_VAL_MSB 0x40
/* Macro to define the accel FOC range */
#define BMI3_ACC_FOC_2G_REF 16384
#define BMI3_ACC_FOC_4G_REF 8192
#define BMI3_ACC_FOC_8G_REF 4096
#define BMI3_ACC_FOC_16G_REF 2048
#define BMI3_FOC_SAMPLE_LIMIT 32

#define FOC_TRY_COUNT 5
/* 20ms delay for 50Hz ODR */
#define FOC_DELAY 20
#define OFFSET_UPDATE_DELAY 120
#define BMI3_INT_STATUS_FWM 0x4000
#define BMI3_INT_STATUS_FFULL 0x8000
#define BMI3_INT_STATUS_ORIENTATION 0x0008

#define BMI3_FIFO_GYRO_I2C_SYNC_FRAME 0x7f02
#define BMI3_FIFO_ACCEL_I2C_SYNC_FRAME 0x7f01

/* Gyro self calibration address */
#define BMI3_BASE_ADDR_SC 0x26
#define BMI3_CMD_SELF_CALIB 0x0101

/* Feature engine General purpose register 1. */
#define BMI3_FEATURE_IO_0 0x10
#define BMI3_ANY_MOTION_X_EN_MASK 0x08

#define BMI3_FEATURE_IO_1 0x11
#define BMI3_FEATURE_IO_1_ERROR_MASK 0x0F
#define BMI3_FEATURE_IO_1_NO_ERROR 0x05
#define BMI3_SC_ST_STATUS_MASK 0x10
#define BMI3_SC_RESULT_MASK 0x20
#define BMI3_UGAIN_OFFS_UPD_COMPLETE 0x01

#define BMI3_FEATURE_IO_STATUS 0x14

/*
 * The max positive value of accel data is 0x7FFF, equal to range(g)
 * So, in order to get +1g, divide the 0x7FFF by range
 */
#define BMI3_ACC_DATA_PLUS_1G(range) (0x7FFF / (range))
#define BMI3_ACC_DATA_MINUS_1G(range) (-BMI3_ACC_DATA_PLUS_1G(range))

/* Offset DMA registers */
#define BMI3_ACC_OFFSET_ADDR 0x40
#define BMI3_GYRO_OFFSET_ADDR 0x46

/*
 * Start address of the DMA transaction. Has to be written to initiate a
 * transaction.
 */
#define BMI3_FEATURE_ENGINE_DMA_TX 0x41

/* DMA read/write data. On read transaction expect first word to be zero. */
#define BMI3_FEATURE_ENGINE_DMA_TX_DATA 0x42

/* Command for offset update */
#define BMI3_CMD_USR_GAIN_OFFS_UPDATE 0x301

/* 1LSB - 31 Micro-G */
#define BMI3_OFFSET_ACC_MULTI_MG (31 * 1000)

/* 1LSB = 61 milli-dps*/
#define BMI3_OFFSET_GYR_MDPS (61 * 1000)

#define BMI3_FIFO_BUFFER 32

/* General Macro Definitions */
/* LSB and MSB mask definitions */
#define BMI3_SET_LOW_BYTE 0x00FF
#define BMI3_SET_HIGH_BYTE 0xFF00

/* For enable and disable */
#define BMI3_ENABLE 0x1
#define BMI3_DISABLE 0x0

/* Defines mode of operation for Accelerometer */
#define BMI3_POWER_MODE_MASK 0x70
#define BMI3_POWER_MODE_POS 4

#define BMI3_SENS_ODR_MASK 0x0F

/* Full scale, Resolution */
#define BMI3_SENS_RANGE_MASK 0x70
#define BMI3_SENS_RANGE_POS 4

#define BMI3_CHIP_ID_MASK 0xFF

/* Map FIFO water-mark interrupt to either INT1 or INT2 or IBI */
#define BMI3_FWM_INT_MASK 0x30
#define BMI3_FWM_INT_POS 4

/* Map FIFO full interrupt to either INT1 or INT2 or IBI */
#define BMI3_FFULL_INT_MASK 0xC0
#define BMI3_FFULL_INT_POS 6

#define BMI3_ORIENT_INT_MASK 0xC0
#define BMI3_ORIENT_INT_POS 6

/*  Mask definitions for interrupt pin configuration */
#define BMI3_INT_LATCH_MASK 0x0001

/**
 * Current fill level of FIFO buffer
 * An empty FIFO corresponds to 0x000. The word counter may be reset by reading
 * out all frames from the FIFO buffer or when the FIFO is reset through
 * fifo_flush. The word counter is updated each time a complete frame was read
 * or written.
 */
#define BMI3_FIFO_FILL_LVL_MASK 0x07FF

/* Root mean square noise of 100 Hz accelerometer, units: ug */
#define BMI3_ACCEL_RMS_NOISE_100HZ 1200

/* Enum to define interrupt lines */
enum bmi3_hw_int_pin {
	BMI3_INT_NONE,
	BMI3_INT1,
	BMI3_INT2,
	BMI3_I3C_INT,
	BMI3_INT_PIN_MAX
};

/* Structure to define FIFO frame configuration */
struct bmi3_fifo_frame {
	uint16_t data[BMI3_FIFO_BUFFER + 1];

	/* Available fifo length */
	uint16_t available_fifo_len;
};

enum sensor_index_t {
	FIRST_CONT_SENSOR = 0,
	SENSOR_ACCEL = FIRST_CONT_SENSOR,
	SENSOR_GYRO,
	NUM_OF_PRIMARY_SENSOR,
};

#define BMI3_DRDY_OFF(_sensor) (7 - (_sensor))
#define BMI3_DRDY_MASK(_sensor) (1 << BMI3_DRDY_OFF(_sensor))

/* Utility macros */
#define BMI3_SET_BITS(reg_data, bitname, data) \
	((reg_data & ~(bitname##_MASK)) |      \
	 ((data << bitname##_POS) & bitname##_MASK))

#define BMI3_GET_BITS(reg_data, bitname) \
	((reg_data & (bitname##_MASK)) >> (bitname##_POS))

#define BMI3_SET_BIT_POS0(reg_data, bitname, data) \
	((reg_data & ~(bitname##_MASK)) | (data & bitname##_MASK))

#define BMI3_GET_BIT_POS0(reg_data, bitname) (reg_data & (bitname##_MASK))

extern const struct accelgyro_drv bmi3xx_drv;

void bmi3xx_interrupt(enum gpio_signal signal);

#if defined(CONFIG_ZEPHYR)
#if DT_NODE_EXISTS(DT_ALIAS(bmi3xx_int))
/*
 * Get the motion sensor ID of the BMI3xx sensor that
 * generates the interrupt.
 * The interrupt is converted to the event and transferred to motion
 * sense task that actually handles the interrupt.
 *
 * Here, we use alias to get the motion sensor ID
 *
 * e.g) base_accel is the label of a child node in /motionsense-sensors
 * aliases {
 *     bmi3xx-int = &base_accel;
 * };
 */
#define CONFIG_ACCELGYRO_BMI3XX_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(SENSOR_ID(DT_ALIAS(bmi3xx_int)))
#endif
#endif /* CONFIG_ZEPHYR */

#endif /* __CROS_EC_ACCELGYRO_BMI3XX_H */
