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
#define TCS_I2C_STATUS_ALS_SATURATED        BIT(7)

#define TCS_I2C_AUX_ASL_INT_ENABLE          BIT(5)

/* Light data resides at 0x94 thru 0x98 */
#define TCS_DATA_START_LOCATION             TCS_I2C_CDATAL
#define TCS_CLEAR_DATA_SIZE                 2
#define TCS_RGBC_DATA_SIZE                  8

/* Min and Max sampling frequency in mHz */
#define TCS3400_LIGHT_MIN_FREQ              149
#define TCS3400_LIGHT_MAX_FREQ              1000
#if (CONFIG_EC_MAX_SENSOR_FREQ_MILLIHZ <= TCS3400_LIGHT_MAX_FREQ)
#error "EC too slow for light sensor"
#endif

#define TCS3400_DRV_DATA(_s) ((struct als_drv_data_t *)(_s)->drv_data)
#define TCS3400_RGB_DRV_DATA(_s) \
	((struct tcs3400_rgb_drv_data_t *)(_s)->drv_data)

/* NOTE: The higher the ATIME value in reg, the shorter the accumulation time */
#define TCS_MIN_ATIME           0x00            /* 712 ms */
#define TCS_MAX_ATIME           0x70            /* 400 ms */
#define TCS_ATIME_GRANULARITY   256             /* 256 atime settings */
#define TCS_SATURATION_LEVEL    0xffff          /* for 0 < atime < 0x70 */
#define TCS_DEFAULT_ATIME       TCS_MIN_ATIME   /* 712 ms */
#define TCS_CALIBRATION_ATIME   TCS_MIN_ATIME
#define TCS_GAIN_UPSHIFT_ATIME  TCS_MAX_ATIME

/* Number of different ranges supported for atime adjustment support */
#define TCS_MAX_ATIME_RANGES    13
#define TCS_GAIN_TABLE_MAX_LUX  12999
#define TCS_ATIME_GAIN_FACTOR   100     /* table values are 100x actual value */

#define TCS_MIN_AGAIN           0x00            /* 1x gain */
#define TCS_MAX_AGAIN           0x03            /* 64x gain */
#define TCS_CALIBRATION_AGAIN   0x02            /* 16x gain */
#define TCS_DEFAULT_AGAIN       TCS_CALIBRATION_AGAIN

#define TCS_ATIME_DEC_STEP      5
#define TCS_ATIME_INC_STEP      TCS_GAIN_UPSHIFT_ATIME

/*
 * Factor to multiply light value by to determine if an increase in gain
 * would cause the next value to saturate.
 *
 * On the TCS3400, gain increases 4x each time again register setting is
 * incremented.  However, I see cases where values that are 24% of saturation
 * go into saturation after increasing gain, causing a back-and-forth cycle to
 * occur :
 *
 * [134.654994 tcs3400_adjust_sensor_for_saturation value=65535 100% Gain=2 ]
 * [135.655064 tcs3400_adjust_sensor_for_saturation value=15750 24% Gain=1 ]
 * [136.655107 tcs3400_adjust_sensor_for_saturation value=65535 100% Gain=2 ]
 *
 * To avoid this, we require value to be <= 20% of saturation level
 * (TCS_GAIN_SAT_LEVEL) before allowing gain to be increased.
 */
#define TCS_GAIN_ADJUST_FACTOR   5
#define TCS_GAIN_SAT_LEVEL       (TCS_SATURATION_LEVEL / TCS_GAIN_ADJUST_FACTOR)
#define TCS_UPSHIFT_FACTOR_N     25 /* upshift factor = 2.5 */
#define TCS_UPSHIFT_FACTOR_D     10
#define TCS_GAIN_UPSHIFT_LEVEL   (TCS_SATURATION_LEVEL * TCS_UPSHIFT_FACTOR_D \
							/ TCS_UPSHIFT_FACTOR_N)

/*
 * Percentage of saturation level that the auto-adjusting anti-saturation
 * method will drive towards.
 */
#define TSC_SATURATION_LOW_BAND_PERCENT 90
#define TSC_SATURATION_LOW_BAND_LEVEL   (TCS_SATURATION_LEVEL * \
					 TSC_SATURATION_LOW_BAND_PERCENT / 100)

enum crbg_index {
	CLEAR_CRGB_IDX = 0,
	RED_CRGB_IDX,
	GREEN_CRGB_IDX,
	BLUE_CRGB_IDX,
	CRGB_COUNT,
};

/* saturation auto-adjustment */
struct tcs_saturation_t {
	/*
	 * Gain Scaling; must be value between 0 and 3
	 *      0 - 1x scaling
	 *      1 - 4x scaling
	 *      2 - 16x scaling
	 *      3 - 64x scaling
	 */
	uint8_t again;

	/* Acquisition Time, controlled by the ATIME register */
	uint8_t atime;             /* ATIME register setting */
};

/* tcs3400 rgb als driver data */
struct tcs3400_rgb_drv_data_t {
	uint8_t calibration_mode;/* 0 = normal run mode, 1 = calibration mode */

	struct rgb_calibration_t calibration;
	struct tcs_saturation_t saturation;  /* saturation adjustment */
};

extern const struct accelgyro_drv tcs3400_drv;
extern const struct accelgyro_drv tcs3400_rgb_drv;

void tcs3400_interrupt(enum gpio_signal signal);
#endif /* __CROS_EC_ALS_TCS3400_H */
