/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AMS TCS3400 light sensor driver
 */
#include "accelgyro.h"
#include "als_tcs3400.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "hwtimer.h"
#include "i2c.h"
#include "math_util.h"
#include "motion_sense_fifo.h"
#include "task.h"
#include "util.h"

#ifdef CONFIG_ALS_TCS3400_INT_EVENT
#define ALS_TCS3400_INT_ENABLE
#endif

#define CPRINTS(fmt, args...) cprints(CC_ACCEL, "%s " fmt, __func__, ##args)

volatile uint32_t last_interrupt_timestamp;

static void decrement_atime(struct tcs_saturation_t *sat_p,
			    uint16_t __attribute__((unused)) cur_lux,
			    int __attribute__((unused)) percent)
{
	sat_p->atime = MAX(sat_p->atime - TCS_ATIME_DEC_STEP, TCS_MIN_ATIME);
}

static void increment_atime(struct tcs_saturation_t *sat_p)
{
	sat_p->atime = MIN(sat_p->atime + TCS_ATIME_INC_STEP, TCS_MAX_ATIME);
}

static inline int tcs3400_i2c_read8(const struct motion_sensor_t *s, int reg,
				    int *data)
{
	return i2c_read8(s->port, s->i2c_spi_addr_flags, reg, data);
}

static inline int tcs3400_i2c_write8(const struct motion_sensor_t *s, int reg,
				     int data)
{
	return i2c_write8(s->port, s->i2c_spi_addr_flags, reg, data);
}

#ifdef CONFIG_ALS_TCS3400_EMULATED_IRQ_EVENT
static void tcs3400_read_deferred(void)
#else
void tcs3400_interrupt(enum gpio_signal signal)
#endif
{
	last_interrupt_timestamp = __hw_clock_source_read();

	task_set_event(TASK_ID_MOTIONSENSE, CONFIG_ALS_TCS3400_INT_EVENT);
}
#ifdef CONFIG_ALS_TCS3400_EMULATED_IRQ_EVENT
DECLARE_DEFERRED(tcs3400_read_deferred);
#endif

/* convert ATIME register to integration time, in microseconds */
int tcs3400_get_integration_time(int atime)
{
	return TCS_MAX_INTEGRATION_TIME * (TCS_ATIME_GRANULARITY - atime);
}

static int tcs3400_read(const struct motion_sensor_t *s, intv3_t v)
{
	int atime, again;
	int ret;

	/* Chip may have been off, make sure to setup important registers */
	if (TCS3400_RGB_DRV_DATA(s + 1)->calibration_mode) {
		atime = TCS_CALIBRATION_ATIME;
		again = TCS_CALIBRATION_AGAIN;
	} else {
		atime = TCS3400_RGB_DRV_DATA(s + 1)->saturation.atime;
		again = TCS3400_RGB_DRV_DATA(s + 1)->saturation.again;
	}
	ret = tcs3400_i2c_write8(s, TCS_I2C_ATIME, atime);
	if (ret)
		return ret;
	ret = tcs3400_i2c_write8(s, TCS_I2C_CONTROL, again);
	if (ret)
		return ret;

	/* Enable power, ADC, and interrupt to start cycle */
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, TCS3400_MODE_COLLECTING);
	if (ret)
		return ret;

#ifdef CONFIG_ALS_TCS3400_EMULATED_IRQ_EVENT
	hook_call_deferred(&tcs3400_read_deferred_data,
			   tcs3400_get_integration_time(atime));
#endif

	/*
	 * If write succeeded, we've started the read process, but can't
	 * complete it yet until data is ready, so pass back EC_RES_IN_PROGRESS
	 * to inform upper level that read data process is under way and data
	 * will be delivered when available.
	 */
	return EC_RES_IN_PROGRESS;
}

static int tcs3400_rgb_read(const struct motion_sensor_t *s, intv3_t v)
{
	ccprintf("WARNING: tcs3400_rgb_read() should never be called\n");
	return EC_SUCCESS;
}

/*
 * tcs3400_adjust_sensor_for_saturation() tries to keep CRGB values as
 * close to saturation as possible without saturating by implementing
 * the following logic:
 *
 * If any of the R, G, B, or C channels have saturated, then decrease AGAIN.
 * If AGAIN is already at its minimum, increase ATIME if not at its max already.
 *
 * Else if none of the R, G, B, or C channels have saturated, and
 * all samples read are less than 90% of saturation, then increase
 * AGAIN if it is not already at its maximum, or if it is, decrease
 * ATIME if it is not at it's minimum already.
 */
static int tcs3400_adjust_sensor_for_saturation(struct motion_sensor_t *s,
						uint16_t cur_lux,
						uint16_t *crgb_data,
						uint32_t status)
{
	struct tcs_saturation_t *sat_p =
		&TCS3400_RGB_DRV_DATA(s + 1)->saturation;
	const uint8_t save_again = sat_p->again;
	const uint8_t save_atime = sat_p->atime;
	uint16_t max_val = 0;
	int ret;
	int percent_left = 0;

	/* Adjust for saturation if needed */
	if (!(status & TCS_I2C_STATUS_RGBC_VALID))
		return EC_SUCCESS;

	for (int i = 0; i < CRGB_COUNT; i++)
		max_val = MAX(max_val, crgb_data[i]);

	/* Don't process if status isn't valid yet */
	if ((status & TCS_I2C_STATUS_ALS_SATURATED) ||
	    (max_val >= TCS_SATURATION_LEVEL)) {
		/* Saturation occurred, decrease AGAIN if we can */
		if (sat_p->again > TCS_MIN_AGAIN)
			sat_p->again--;
		else if (sat_p->atime < TCS_MAX_ATIME)
			/* reduce accumulation time by incrementing ATIME reg */
			increment_atime(sat_p);
	} else if (max_val < TSC_SATURATION_LOW_BAND_LEVEL) {
		/* value < 90% saturation, try to increase sensitivity */
		if (max_val <= TCS_GAIN_SAT_LEVEL) {
			if (sat_p->again < TCS_MAX_AGAIN) {
				sat_p->again++;
			} else if (sat_p->atime > TCS_MIN_ATIME) {
				/*
				 * increase accumulation time by decrementing
				 * ATIME register
				 */
				percent_left =
					TSC_SATURATION_LOW_BAND_PERCENT -
					(max_val * 100 / TCS_SATURATION_LEVEL);
				decrement_atime(sat_p, cur_lux, percent_left);
			}
		} else if (sat_p->atime > TCS_MIN_ATIME) {
			/* calculate percentage between current and desired */
			percent_left = TSC_SATURATION_LOW_BAND_PERCENT -
				       (max_val * 100 / TCS_SATURATION_LEVEL);

			/* increase accumulation time by decrementing ATIME */
			decrement_atime(sat_p, cur_lux, percent_left);
		} else if (sat_p->again < TCS_MAX_AGAIN) {
			/*
			 * Although we're not at maximum gain yet, we
			 * can't just increase gain because a 4x change
			 * in gain under these light conditions would
			 * saturate on the next sample.  What we can do
			 * is to adjust atime to reduce sensitivity so
			 * that we may increase gain without saturation.
			 * This combination effectively acts as a half
			 * gain increase (2.5x estimate) instead of a full
			 * gain increase of > 4x that would result in
			 * saturation.
			 */
			if (max_val < TCS_GAIN_UPSHIFT_LEVEL) {
				sat_p->atime = TCS_GAIN_UPSHIFT_ATIME;
				sat_p->again++;
			}
		}
	}

	/* If atime or gain setting changed, update atime and gain registers */
	if (save_again != sat_p->again) {
		ret = tcs3400_i2c_write8(s, TCS_I2C_CONTROL,
					 (sat_p->again & TCS_I2C_CONTROL_MASK));
		if (ret)
			return ret;
	}

	if (save_atime != sat_p->atime) {
		ret = tcs3400_i2c_write8(s, TCS_I2C_ATIME, sat_p->atime);
		if (ret)
			return ret;
	}

	return EC_SUCCESS;
}

/**
 * normalize_channel_data - normalize the light data to remove effect of
 * different atime and again settings from the sample.
 */
static uint32_t normalize_channel_data(struct motion_sensor_t *s,
				       uint32_t sample)
{
	struct tcs_saturation_t *sat_p =
		&(TCS3400_RGB_DRV_DATA(s + 1)->saturation);
	const uint16_t cur_gain = (1 << (2 * sat_p->again));
	const uint16_t cal_again = (1 << (2 * TCS_CALIBRATION_AGAIN));

	return DIV_ROUND_NEAREST(
		sample * (TCS_ATIME_GRANULARITY - TCS_CALIBRATION_ATIME) *
			cal_again,
		(TCS_ATIME_GRANULARITY - sat_p->atime) * cur_gain);
}

__overridable void tcs3400_translate_to_xyz(struct motion_sensor_t *s,
					    int32_t *crgb_data,
					    int32_t *xyz_data)
{
	struct tcs3400_rgb_drv_data_t *rgb_drv_data =
		TCS3400_RGB_DRV_DATA(s + 1);
	int32_t crgb_prime[CRGB_COUNT];
	int32_t ir;
	int i;

	/* normalize the data for atime and again changes */
	for (i = 0; i < CRGB_COUNT; i++)
		crgb_data[i] = normalize_channel_data(s, crgb_data[i]);

	/* IR removal */
	ir = FP_TO_INT(fp_mul(INT_TO_FP(crgb_data[1] + crgb_data[2] +
					crgb_data[3] - crgb_data[0]),
			      rgb_drv_data->calibration.irt) /
		       2);

	for (i = 0; i < ARRAY_SIZE(crgb_prime); i++) {
		if (crgb_data[i] < ir)
			crgb_prime[i] = 0;
		else
			crgb_prime[i] = crgb_data[i] - ir;
	}

	/* if CC == 0, set BC = 0 */
	if (crgb_prime[CLEAR_CRGB_IDX] == 0)
		crgb_prime[BLUE_CRGB_IDX] = 0;

	/* regression fit to XYZ space */
	for (i = 0; i < 3; i++) {
		const struct rgb_channel_calibration_t *p =
			&rgb_drv_data->calibration.rgb_cal[i];

		xyz_data[i] = p->offset +
			      FP_TO_INT((fp_inter_t)p->coeff[RED_CRGB_IDX] *
						crgb_prime[RED_CRGB_IDX] +
					(fp_inter_t)p->coeff[GREEN_CRGB_IDX] *
						crgb_prime[GREEN_CRGB_IDX] +
					(fp_inter_t)p->coeff[BLUE_CRGB_IDX] *
						crgb_prime[BLUE_CRGB_IDX] +
					(fp_inter_t)p->coeff[CLEAR_CRGB_IDX] *
						crgb_prime[CLEAR_CRGB_IDX]);

		if (xyz_data[i] < 0)
			xyz_data[i] = 0;
	}
}

static void tcs3400_process_raw_data(struct motion_sensor_t *s,
				     uint8_t *raw_data_buf,
				     uint16_t *raw_light_data,
				     int32_t *xyz_data)
{
	struct als_drv_data_t *als_drv_data = TCS3400_DRV_DATA(s);
	struct tcs3400_rgb_drv_data_t *rgb_drv_data =
		TCS3400_RGB_DRV_DATA(s + 1);
	const uint8_t calibration_mode = rgb_drv_data->calibration_mode;
	uint16_t k_channel_scale =
		als_drv_data->als_cal.channel_scale.k_channel_scale;
	uint16_t cover_scale = als_drv_data->als_cal.channel_scale.cover_scale;
	int32_t crgb_data[CRGB_COUNT];
	int i;

	/* adjust for calibration and scale data */
	for (i = 0; i < CRGB_COUNT; i++) {
		int index = i * 2;

		/* assemble the light value for this channel */
		crgb_data[i] = raw_light_data[i] =
			((raw_data_buf[index + 1] << 8) | raw_data_buf[index]);

		/* in calibration mode, we only assemble the raw data */
		if (calibration_mode)
			continue;

		/* rgb data at index 1, 2, and 3 owned by rgb driver, not ALS */
		if (i > 0) {
			struct als_channel_scale_t *csp =
				&rgb_drv_data->calibration.rgb_cal[i - 1].scale;
			k_channel_scale = csp->k_channel_scale;
			cover_scale = csp->cover_scale;
		}

		/* Step 1: divide by individual channel scale value */
		crgb_data[i] =
			SENSOR_APPLY_DIV_SCALE(crgb_data[i], k_channel_scale);

		/* compensate for the light cover */
		crgb_data[i] = SENSOR_APPLY_SCALE(crgb_data[i], cover_scale);
	}

	if (!calibration_mode) {
		/* we're not in calibration mode & we want xyz translation */
		tcs3400_translate_to_xyz(s, crgb_data, xyz_data);
	} else {
		/* normalize the data for atime and again changes */
		for (i = 0; i < CRGB_COUNT; i++)
			crgb_data[i] = normalize_channel_data(s, crgb_data[i]);

		/* calibration mode returns raw data */
		for (i = 0; i < 3; i++)
			xyz_data[i] = crgb_data[i + 1];
	}
}

static int32_t get_lux_from_xyz(struct motion_sensor_t *s, int32_t *xyz_data)
{
	int32_t lux = xyz_data[Y];
	const int32_t offset =
		TCS3400_RGB_DRV_DATA(s + 1)->calibration.rgb_cal[Y].offset;

	/*
	 * Do not include the offset when determining LUX from XYZ.
	 */
	lux = MAX(0, lux - offset);

	return lux;
}

static bool is_spoof(struct motion_sensor_t *s)
{
	return IS_ENABLED(CONFIG_ACCEL_SPOOF_MODE) &&
	       (s->flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE);
}

static int tcs3400_post_events(struct motion_sensor_t *s, uint32_t last_ts,
			       uint32_t status)
{
	/*
	 * Rule says RGB sensor is right after ALS sensor.
	 * This routine will only get called from ALS sensor driver.
	 */
	struct motion_sensor_t *rgb_s = s + 1;
	const uint8_t is_calibration =
		TCS3400_RGB_DRV_DATA(rgb_s)->calibration_mode;
	uint8_t buf[TCS_RGBC_DATA_SIZE]; /* holds raw data read from chip */
	int32_t xyz_data[3] = { 0, 0, 0 };
	uint16_t raw_data[CRGB_COUNT]; /* holds raw CRGB assembled from buf[] */
	int *last_v;
	int32_t lux = 0;
	int ret;

	if (IS_ENABLED(CONFIG_ALS_TCS3400_EMULATED_IRQ_EVENT)) {
		int i = 5; /* 100ms max */

		while (i--) {
			/* Make sure data is valid */
			if (status & TCS_I2C_STATUS_RGBC_VALID)
				break;
			crec_msleep(20);
			/*
			 * When not in interrupt mode, we could have scheduled
			 * the handler too early.
			 */
			ret = tcs3400_i2c_read8(s, TCS_I2C_STATUS, &status);
			if (ret)
				return ret;
		}
		if (i < 0) {
			CPRINTS("RGBC invalid (0x%x)", status);
			return EC_ERROR_UNCHANGED;
		}
	}

	/* Read the light registers */
	ret = i2c_read_block(s->port, s->i2c_spi_addr_flags,
			     TCS_DATA_START_LOCATION, buf, sizeof(buf));
	if (ret)
		return ret;

	/* Process the raw light data, adjusting for scale and calibration */
	tcs3400_process_raw_data(s, buf, raw_data, xyz_data);

	/* get lux value */
	lux = is_calibration ? xyz_data[Y] : get_lux_from_xyz(s, xyz_data);

	/* if clear channel data changed, send illuminance upstream */
	last_v = s->raw_xyz;
	if (is_calibration ||
	    ((raw_data[CLEAR_CRGB_IDX] != TCS_SATURATION_LEVEL) &&
	     (last_v[X] != lux))) {
		if (is_spoof(s))
			last_v[X] = s->spoof_xyz[X];
		else
			last_v[X] = is_calibration ? raw_data[CLEAR_CRGB_IDX] :
						     lux;

		if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
			struct ec_response_motion_sensor_data vector = {
				.flags = 0,
			};

			vector.udata[X] = ec_motion_sensor_clamp_u16(last_v[X]);
			vector.udata[Y] = 0;
			vector.udata[Z] = 0;

			vector.sensor_num = s - motion_sensors;
			motion_sense_fifo_stage_data(&vector, s, 3, last_ts);
		} else {
			motion_sense_push_raw_xyz(s);
		}
	}

	/*
	 * If rgb channel data changed since last sample and didn't saturate,
	 * send it upstream
	 */
	last_v = rgb_s->raw_xyz;
	if (is_calibration ||
	    (((last_v[X] != xyz_data[X]) || (last_v[Y] != xyz_data[Y]) ||
	      (last_v[Z] != xyz_data[Z])) &&
	     ((raw_data[RED_CRGB_IDX] != TCS_SATURATION_LEVEL) &&
	      (raw_data[BLUE_CRGB_IDX] != TCS_SATURATION_LEVEL) &&
	      (raw_data[GREEN_CRGB_IDX] != TCS_SATURATION_LEVEL)))) {
		if (is_spoof(rgb_s)) {
			memcpy(last_v, rgb_s->spoof_xyz,
			       sizeof(rgb_s->spoof_xyz));
		} else if (is_calibration) {
			last_v[0] = raw_data[RED_CRGB_IDX];
			last_v[1] = raw_data[GREEN_CRGB_IDX];
			last_v[2] = raw_data[BLUE_CRGB_IDX];
		} else {
			memcpy(last_v, xyz_data, sizeof(xyz_data));
		}

		if (IS_ENABLED(CONFIG_ACCEL_FIFO)) {
			struct ec_response_motion_sensor_data vector = {
				.flags = 0,
			};
			void *udata = vector.udata;

			ec_motion_sensor_clamp_u16s(udata, last_v);

			vector.sensor_num = rgb_s - motion_sensors;
			motion_sense_fifo_stage_data(&vector, rgb_s, 3,
						     last_ts);
		} else {
			motion_sense_push_raw_xyz(rgb_s);
		}
	}
	if (IS_ENABLED(CONFIG_ACCEL_FIFO))
		motion_sense_fifo_commit_data();

	if (!is_calibration)
		ret = tcs3400_adjust_sensor_for_saturation(s, xyz_data[Y],
							   raw_data, status);

	return ret;
}

/*
 * tcs3400_irq_handler - bottom half of the interrupt stack.
 * Ran from the motion_sense task, finds the events that raised the interrupt,
 * and posts those events via motion_sense_fifo_stage_data()..
 *
 * This routine will get called for the TCS3400 ALS driver, but NOT for the
 * RGB driver.  We harvest data for both drivers in this routine.  The RGB
 * driver is guaranteed to directly follow the ALS driver in the sensor list
 * (i.e rgb's motion_sensor_t structure can be found at (s+1) ).
 */
static int tcs3400_irq_handler(struct motion_sensor_t *s, uint32_t *event)
{
	uint32_t status = 0;
	int ret;

	if (!(*event & CONFIG_ALS_TCS3400_INT_EVENT))
		return EC_ERROR_NOT_HANDLED;

	ret = tcs3400_i2c_read8(s, TCS_I2C_STATUS, &status);
	if (ret)
		return ret;

	/* Disable future interrupts */
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, TCS3400_MODE_IDLE);
	if (ret)
		return ret;

	if ((status & TCS_I2C_STATUS_RGBC_VALID) ||
	    IS_ENABLED(CONFIG_ALS_TCS3400_EMULATED_IRQ_EVENT)) {
		ret = tcs3400_post_events(s, last_interrupt_timestamp, status);
		if (ret)
			return ret;
	}

	tcs3400_i2c_write8(s, TCS_I2C_AICLEAR, 0);

	/* Disable ADC and turn off internal oscillator */
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, TCS3400_MODE_SUSPEND);
	if (ret)
		return ret;

	return EC_SUCCESS;
}

static int tcs3400_rgb_get_scale(const struct motion_sensor_t *s,
				 uint16_t *scale, int16_t *temp)
{
	struct rgb_channel_calibration_t *rgb_cal =
		TCS3400_RGB_DRV_DATA(s)->calibration.rgb_cal;

	scale[X] = rgb_cal[RED_RGB_IDX].scale.k_channel_scale;
	scale[Y] = rgb_cal[GREEN_RGB_IDX].scale.k_channel_scale;
	scale[Z] = rgb_cal[BLUE_RGB_IDX].scale.k_channel_scale;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_rgb_set_scale(const struct motion_sensor_t *s,
				 const uint16_t *scale, int16_t temp)
{
	struct rgb_channel_calibration_t *rgb_cal =
		TCS3400_RGB_DRV_DATA(s)->calibration.rgb_cal;

	if (scale[X] == 0 || scale[Y] == 0 || scale[Z] == 0)
		return EC_ERROR_INVAL;
	rgb_cal[RED_RGB_IDX].scale.k_channel_scale = scale[X];
	rgb_cal[GREEN_RGB_IDX].scale.k_channel_scale = scale[Y];
	rgb_cal[BLUE_RGB_IDX].scale.k_channel_scale = scale[Z];
	return EC_SUCCESS;
}

static int tcs3400_rgb_get_offset(const struct motion_sensor_t *s,
				  int16_t *offset, int16_t *temp)
{
	offset[X] = TCS3400_RGB_DRV_DATA(s)->calibration.rgb_cal[X].offset;
	offset[Y] = TCS3400_RGB_DRV_DATA(s)->calibration.rgb_cal[Y].offset;
	offset[Z] = TCS3400_RGB_DRV_DATA(s)->calibration.rgb_cal[Z].offset;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_rgb_set_offset(const struct motion_sensor_t *s,
				  const int16_t *offset, int16_t temp)
{
	/* do not allow offset to be changed, it's predetermined */
	return EC_SUCCESS;
}

static int tcs3400_rgb_set_data_rate(const struct motion_sensor_t *s, int rate,
				     int rnd)
{
	return EC_SUCCESS;
}

/* Enable/disable special factory calibration mode */
static int tcs3400_perform_calib(struct motion_sensor_t *s, int enable)
{
	TCS3400_RGB_DRV_DATA(s + 1)->calibration_mode = enable;
	return EC_SUCCESS;
}

static int tcs3400_rgb_set_range(struct motion_sensor_t *s, int range, int rnd)
{
	return EC_SUCCESS;
}

static int tcs3400_set_range(struct motion_sensor_t *s, int range, int rnd)
{
	TCS3400_DRV_DATA(s)->als_cal.scale = range >> 16;
	TCS3400_DRV_DATA(s)->als_cal.uscale = range & 0xffff;
	s->current_range = range;
	return EC_SUCCESS;
}

static int tcs3400_get_scale(const struct motion_sensor_t *s, uint16_t *scale,
			     int16_t *temp)
{
	scale[X] = TCS3400_DRV_DATA(s)->als_cal.channel_scale.k_channel_scale;
	scale[Y] = 0;
	scale[Z] = 0;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_set_scale(const struct motion_sensor_t *s,
			     const uint16_t *scale, int16_t temp)
{
	if (scale[X] == 0)
		return EC_ERROR_INVAL;
	TCS3400_DRV_DATA(s)->als_cal.channel_scale.k_channel_scale = scale[X];
	return EC_SUCCESS;
}

static int tcs3400_get_offset(const struct motion_sensor_t *s, int16_t *offset,
			      int16_t *temp)
{
	offset[X] = TCS3400_DRV_DATA(s)->als_cal.offset;
	offset[Y] = 0;
	offset[Z] = 0;
	*temp = EC_MOTION_SENSE_INVALID_CALIB_TEMP;
	return EC_SUCCESS;
}

static int tcs3400_set_offset(const struct motion_sensor_t *s,
			      const int16_t *offset, int16_t temp)
{
	/* do not allow offset to be changed, it's predetermined */
	return EC_SUCCESS;
}

static int tcs3400_get_data_rate(const struct motion_sensor_t *s)
{
	return TCS3400_DRV_DATA(s)->rate;
}

static int tcs3400_rgb_get_data_rate(const struct motion_sensor_t *s)
{
	return tcs3400_get_data_rate(s - 1);
}

static int tcs3400_set_data_rate(const struct motion_sensor_t *s, int rate,
				 int rnd)
{
	enum tcs3400_mode mode;
	int data;
	int ret;

	if (rate == 0) {
		/* Suspend driver */
		mode = TCS3400_MODE_SUSPEND;
	} else {
		/*
		 * We set the sensor for continuous mode,
		 * integrating over 800ms.
		 * Do not allow range higher than 1Hz.
		 */
		if (rate > TCS3400_LIGHT_MAX_FREQ)
			rate = TCS3400_LIGHT_MAX_FREQ;
		mode = TCS3400_MODE_COLLECTING;
	}
	TCS3400_DRV_DATA(s)->rate = rate;

	ret = tcs3400_i2c_read8(s, TCS_I2C_ENABLE, &data);
	if (ret)
		return ret;

	data = (data & ~TCS_I2C_ENABLE_MASK) | mode;
	ret = tcs3400_i2c_write8(s, TCS_I2C_ENABLE, data);

	return ret;
}

/**
 * Initialise TCS3400 light sensor.
 */
static int tcs3400_rgb_init(struct motion_sensor_t *s)
{
	return EC_SUCCESS;
}

static int tcs3400_init(struct motion_sensor_t *s)
{
	/*
	 * These are default power-on register values with two exceptions:
	 * Set ATIME = 0 (712 ms)
	 * Set AGAIN = 16 (0x10)  (AGAIN is in CONTROL register)
	 */
	const struct reg_data {
		uint8_t reg;
		uint8_t data;
	} defaults[] = { { TCS_I2C_ENABLE, 0 },
			 { TCS_I2C_ATIME, TCS_DEFAULT_ATIME },
			 { TCS_I2C_WTIME, 0xFF },
			 { TCS_I2C_AILTL, 0 },
			 { TCS_I2C_AILTH, 0 },
			 { TCS_I2C_AIHTL, 0 },
			 { TCS_I2C_AIHTH, 0 },
			 { TCS_I2C_PERS, 0 },
			 { TCS_I2C_CONFIG, 0x40 },
			 { TCS_I2C_CONTROL,
			   (TCS_DEFAULT_AGAIN & TCS_I2C_CONTROL_MASK) },
			 { TCS_I2C_AUX, 0 },
			 { TCS_I2C_IR, 0 },
			 { TCS_I2C_CICLEAR, 0 },
			 { TCS_I2C_AICLEAR, 0 } };
	int data = 0;
	int ret;

	ret = tcs3400_i2c_read8(s, TCS_I2C_ID, &data);
	if (ret) {
		CPRINTS("failed reading ID reg 0x%x, ret=%d", TCS_I2C_ID, ret);
		return ret;
	}
	if ((data != TCS340015_DEVICE_ID) && (data != TCS340037_DEVICE_ID)) {
		CPRINTS("no ID match, data = 0x%x", data);
		return EC_ERROR_ACCESS_DENIED;
	}

	/* reset chip to default power-on settings, changes ATIME and CONTROL */
	for (int x = 0; x < ARRAY_SIZE(defaults); x++) {
		ret = tcs3400_i2c_write8(s, defaults[x].reg, defaults[x].data);
		if (ret)
			return ret;
	}

	return sensor_init_done(s);
}

const struct accelgyro_drv tcs3400_drv = {
	.init = tcs3400_init,
	.read = tcs3400_read,
	.set_range = tcs3400_set_range,
	.set_offset = tcs3400_set_offset,
	.get_offset = tcs3400_get_offset,
	.set_scale = tcs3400_set_scale,
	.get_scale = tcs3400_get_scale,
	.set_data_rate = tcs3400_set_data_rate,
	.get_data_rate = tcs3400_get_data_rate,
	.perform_calib = tcs3400_perform_calib,
#ifdef ALS_TCS3400_INT_ENABLE
	.irq_handler = tcs3400_irq_handler,
#endif
};

const struct accelgyro_drv tcs3400_rgb_drv = {
	.init = tcs3400_rgb_init,
	.read = tcs3400_rgb_read,
	.set_range = tcs3400_rgb_set_range,
	.set_offset = tcs3400_rgb_set_offset,
	.get_offset = tcs3400_rgb_get_offset,
	.set_scale = tcs3400_rgb_set_scale,
	.get_scale = tcs3400_rgb_get_scale,
	.set_data_rate = tcs3400_rgb_set_data_rate,
	.get_data_rate = tcs3400_rgb_get_data_rate,
};
