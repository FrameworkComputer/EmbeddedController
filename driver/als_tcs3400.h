/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AMS TCS3400 light sensor driver
 */

#ifndef __CROS_EC_ALS_TCS3400_H
#define __CROS_EC_ALS_TCS3400_H

/* I2C Interface */
#define TCS3400_I2C_ADDR_FLAGS	0x39

/* ID for TCS34001 and TCS34005 */
#define TCS340015_DEVICE_ID	0x90

/* ID for TCS34003 and TCS34007 */
#define TCS340037_DEVICE_ID	0x93

/* Register Map */
#define TCS_I2C_ENABLE    0x80    /* R/W Enables states and interrupts */
#define TCS_I2C_ATIME     0x81    /* R/W RGBC integration time */
#define TCS_I2C_WTIME     0x83    /* R/W Wait time */
#define TCS_I2C_AILTL     0x84    /* R/W Clear irq low threshold low byte */
#define TCS_I2C_AILTH     0x85    /* R/W Clear irq low threshold high byte */
#define TCS_I2C_AIHTL     0x86    /* R/W Clear irq high threshold low byte */
#define TCS_I2C_AIHTH     0x87    /* R/W Clear irq high threshold high byte */
#define TCS_I2C_PERS      0x8C    /* R/W Interrupt persistence filter */
#define TCS_I2C_CONFIG    0x8D    /* R/W Configuration */
#define TCS_I2C_CONTROL   0x8F    /* R/W Gain control register */
#define TCS_I2C_AUX       0x90    /* R/W Auxiliary control register */
#define TCS_I2C_REVID     0x91    /* R Revision ID */
#define TCS_I2C_ID        0x92    /* R Device ID */
#define TCS_I2C_STATUS    0x93    /* R Device status */
#define TCS_I2C_CDATAL    0x94    /* R Clear / IR channel low data register */
#define TCS_I2C_CDATAH    0x95    /* R Clear / IR channel high data register */
#define TCS_I2C_RDATAL    0x96    /* R Red ADC low data register */
#define TCS_I2C_RDATAH    0x97    /* R Red ADC high data register */
#define TCS_I2C_GDATAL    0x98    /* R Green ADC low data register */
#define TCS_I2C_GDATAH    0x99    /* R Green ADC high data register */
#define TCS_I2C_BDATAL    0x9A    /* R Blue ADC low data register */
#define TCS_I2C_BDATAH    0x9B    /* R Blue ADC high data register */
#define TCS_I2C_IR        0xC0    /* R/W Access IR Channel */
#define TCS_I2C_IFORCE    0xE4    /* W Force Interrupt */
#define TCS_I2C_CICLEAR   0xE6    /* W Clear channel interrupt clear */
#define TCS_I2C_AICLEAR   0xE7    /* W Clear all interrupts */

#define TCS_I2C_ENABLE_POWER_ON             BIT(0)
#define TCS_I2C_ENABLE_ADC_ENABLE           BIT(1)
#define TCS_I2C_ENABLE_WAIT_ENABLE          BIT(3)
#define TCS_I2C_ENABLE_INT_ENABLE           BIT(4)
#define TCS_I2C_ENABLE_SLEEP_AFTER_INT      BIT(6)
#define TCS_I2C_ENABLE_MASK                 (TCS_I2C_ENABLE_POWER_ON |    \
					     TCS_I2C_ENABLE_ADC_ENABLE |  \
					     TCS_I2C_ENABLE_WAIT_ENABLE | \
					     TCS_I2C_ENABLE_INT_ENABLE |  \
					     TCS_I2C_ENABLE_SLEEP_AFTER_INT)

enum tcs3400_mode {
	TCS3400_MODE_SUSPEND = 0,
	TCS3400_MODE_IDLE =       (TCS_I2C_ENABLE_POWER_ON |
				   TCS_I2C_ENABLE_ADC_ENABLE),
	TCS3400_MODE_COLLECTING = (TCS_I2C_ENABLE_POWER_ON |
				   TCS_I2C_ENABLE_ADC_ENABLE |
				   TCS_I2C_ENABLE_INT_ENABLE),
};

#define TCS_I2C_CONTROL_MASK                0x03
#define TCS_I2C_STATUS_RGBC_VALID           BIT(0)
#define TCS_I2C_STATUS_ALS_IRQ              BIT(4)
#define TCS_I2C_STATUS_ALS_VALID            BIT(7)

#define TCS_I2C_AUX_ASL_INT_ENABLE          BIT(5)

/* Light data resides at 0x94 thru 0x98 */
#define TCS_DATA_START_LOCATION             TCS_I2C_CDATAL
#define TCS_CLEAR_DATA_SIZE                 2
#define TCS_RGBC_DATA_SIZE                  8

/* Min and Max sampling frequency in mHz */
#define TCS3400_LIGHT_MIN_FREQ              149
#define TCS3400_LIGHT_MAX_FREQ              10000
#if (CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ <= TCS3400_LIGHT_MAX_FREQ)
#error "EC too slow for light sensor"
#endif

#define TCS3400_DRV_DATA(_s) ((struct als_drv_data_t *)(_s)->drv_data)
#define TCS3400_RGB_DRV_DATA(_s) \
	((struct tcs3400_rgb_drv_data_t *)(_s)->drv_data)

/* NOTE: The higher the ATIME value in reg, the shorter the accumulation time */
#define TCS_MIN_ATIME           0x00            /* 712 ms */
#define TCS_MAX_ATIME           0x70            /* 400 ms */
#define TCS_DEFAULT_ATIME       TCS_MIN_ATIME   /* 712 ms */

#define TCS_MIN_AGAIN           0x00            /* 1x gain */
#define TCS_MAX_AGAIN           0x03            /* 64x gain */
#define TCS_DEFAULT_AGAIN       0x02            /* 16x gain */

/* tcs3400 rgb als driver data */
struct tcs3400_rgb_drv_data_t {
	/*
	 * device_scale and device_uscale are used to adjust raw rgb channel
	 * values prior to applying any channel-specific scaling required.
	 * raw_value += rgb_cal.offset;
	 * adjusted_value = raw_value * device_scale +
	 *                  raw_value * device_uscale / 10000;
	 */
	uint16_t device_scale;
	uint16_t device_uscale;

	int rate;          /* holds current sensor rate */
	int last_value[3]; /* holds last RGB values */
	struct rgb_calibration_t rgb_cal[3]; /* calibration data */
};

extern const struct accelgyro_drv tcs3400_drv;
extern const struct accelgyro_drv tcs3400_rgb_drv;

void tcs3400_interrupt(enum gpio_signal signal);
#endif /* __CROS_EC_ALS_TCS3400_H */
