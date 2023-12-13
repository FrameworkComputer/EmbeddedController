/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gyro_cal.h"
#include "gyro_cal_init_for_test.h"
#include "gyro_still_det.h"
#include "motion_sense.h"
#include "test_util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float kToleranceGyroRps = 1e-6f;
float kDefaultGravityMps2 = 9.81f;
int kDefaultTemperatureKelvin = 298;

#define NANOS_TO_SEC (1.0e-9f)
#define NANO_PI (3.14159265359f)
/** Unit conversion: milli-degrees to radians. */
#define MDEG_TO_RAD (NANO_PI / 180.0e3f)

#define MSEC_TO_NANOS(x) ((uint64_t)((x) * (uint64_t)(1000000)))
#define SEC_TO_NANOS(x) MSEC_TO_NANOS((x) * (uint64_t)(1000))
#define HZ_TO_PERIOD_NANOS(hz) (SEC_TO_NANOS(1024) / ((uint64_t)((hz) * 1024)))

struct motion_sensor_t motion_sensors[] = {
	[BASE] = {},
	[LID] = {},
};

const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/**
 * This function will return a uniformly distributed random value in the range
 * of (0,1). This is important that 0 and 1 are excluded because of how the
 * value is used in normal_random. For references:
 * - rand() / RAND_MAX yields the range [0,1]
 * - rand() / (RAND_MAX + 1) yields the range [0,1)
 * - (rand() + 1) / (RAND_MAX + 1) yields the range (0, 1)
 *
 * @return A uniformly distributed random value.
 */
static double rand_gen(void)
{
	return ((double)(rand()) + 1.0) / ((double)(RAND_MAX) + 1.0);
}

/**
 * @return A normally distributed random value
 */
static float normal_random(void)
{
	double v1 = rand_gen();
	double v2 = rand_gen();

	return (float)(cos(2 * 3.14 * v2) * sqrt(-2.0 * log(v1)));
}

/**
 * @param mean The mean to use for the normal distribution.
 * @param stddev The standard deviation to use for the normal distribution.
 * @return A normally distributed random value based on mean and stddev.
 */
static float normal_random2(float mean, float stddev)
{
	return normal_random() * stddev + mean;
}

/**
 * Tests that a calibration is updated after a period where the IMU device is
 * stationary. Accelerometer and gyroscope measurements are simulated with data
 * sheet specs for the BMI160 at their respective noise floors. A magnetometer
 * sensor is also included in this test.
 *
 * @return EC_SUCCESS on success.
 */
static int test_gyro_cal_calibration(void)
{
	int i;
	struct gyro_cal gyro_cal;

	/*
	 * Statistics for simulated gyroscope data.
	 * RMS noise = 70mDPS, offset = 150mDPS.
	 */
	/* [Hz] */
	const float sample_rate = 400.0f;
	/* [rad/sec] */
	const float gyro_bias = MDEG_TO_RAD * 150.0f;
	/* [rad/sec] */
	const float gyro_rms_noise = MDEG_TO_RAD * 70.0f;
	const uint64_t sample_interval_nanos = HZ_TO_PERIOD_NANOS(sample_rate);

	/*
	 * Statistics for simulated accelerometer data.
	 * noise density = 200ug/rtHz, offset = 50mg.
	 */
	/* [m/sec^2] */
	const float accel_bias = 0.05f * kDefaultGravityMps2;
	/* [m/sec^2] */
	const float accel_rms_noise =
		0.0002f * kDefaultGravityMps2 * fp_sqrtf(0.5f * sample_rate);

	/*
	 * Statistics for simulated magnetometer data.
	 * RMS noise = 0.4 micro Tesla (uT), offset = 0.2uT.
	 */
	const float mag_bias = 0.2f; /* [uT] */
	const float mag_rms_noise = 0.4f; /* [uT] */

	float bias[3];
	float bias_residual[3];
	int temperature_kelvin;
	uint32_t calibration_time_us = 0;

	bool calibration_received = false;

	gyro_cal_initialization_for_test(&gyro_cal);

	/* No calibration should be available yet. */
	TEST_EQ(gyro_cal_new_bias_available(&gyro_cal), false, "%d");

	/*
	 * Simulate up to 20 seconds of sensor data (zero mean, additive white
	 * Gaussian noise).
	 */
	for (i = 0; i < (int)(20.0f * sample_rate); ++i) {
		const uint32_t timestamp_us =
			(i * sample_interval_nanos) / 1000;

		/* Generate and add an accelerometer sample. */
		gyro_cal_update_accel(
			&gyro_cal, timestamp_us,
			normal_random2(accel_bias, accel_rms_noise),
			normal_random2(accel_bias, accel_rms_noise),
			normal_random2(accel_bias, accel_rms_noise));

		/* Generate and add a gyroscrope sample. */
		gyro_cal_update_gyro(&gyro_cal, timestamp_us,
				     normal_random2(gyro_bias, gyro_rms_noise),
				     normal_random2(gyro_bias, gyro_rms_noise),
				     normal_random2(gyro_bias, gyro_rms_noise),
				     kDefaultTemperatureKelvin);

		/*
		 * The simulated magnetometer here has a sampling rate that is
		 * 4x slower than the accel/gyro
		 */
		if (i % 4 == 0) {
			gyro_cal_update_mag(
				&gyro_cal, timestamp_us,
				normal_random2(mag_bias, mag_rms_noise),
				normal_random2(mag_bias, mag_rms_noise),
				normal_random2(mag_bias, mag_rms_noise));
		}
		calibration_received = gyro_cal_new_bias_available(&gyro_cal);
		if (calibration_received)
			break;
	}

	TEST_EQ(calibration_received, true, "%d");

	gyro_cal_get_bias(&gyro_cal, bias, &temperature_kelvin,
			  &calibration_time_us);
	bias_residual[0] = gyro_bias - bias[0];
	bias_residual[1] = gyro_bias - bias[1];
	bias_residual[2] = gyro_bias - bias[2];

	/*
	 * Make sure that the bias estimate is within 20 milli-degrees per
	 * second.
	 */
	TEST_LT(bias_residual[0], 20.f * MDEG_TO_RAD, "%f");
	TEST_LT(bias_residual[1], 20.f * MDEG_TO_RAD, "%f");
	TEST_LT(bias_residual[2], 20.f * MDEG_TO_RAD, "%f");

	TEST_NEAR(gyro_cal.stillness_confidence, 1.0f, 0.0001f, "%f");

	TEST_EQ(temperature_kelvin, kDefaultTemperatureKelvin, "%d");

	return EC_SUCCESS;
}

/**
 * Tests that calibration does not falsely occur for low-level motion.
 *
 * @return EC_SUCCESS on success.
 */
static int test_gyro_cal_no_calibration(void)
{
	int i;
	struct gyro_cal gyro_cal;

	/* Statistics for simulated gyroscope data. */
	/* RMS noise = 70mDPS, offset = 150mDPS. */
	const float sample_rate = 400.0f; /* [Hz] */
	const float gyro_bias = MDEG_TO_RAD * 150.0f; /* [rad/sec] */
	const float gyro_rms_noise = MDEG_TO_RAD * 70.0f; /* [rad/sec] */
	const uint64_t sample_interval_nanos = HZ_TO_PERIOD_NANOS(sample_rate);

	/* Statistics for simulated accelerometer data. */
	/* noise density = 200ug/rtHz, offset = 50mg. */
	/* [m/sec^2] */
	const float accel_bias = 0.05f * kDefaultGravityMps2;
	/* [m/sec^2] */
	const float accel_rms_noise =
		200.0e-6f * kDefaultGravityMps2 * fp_sqrtf(0.5f * sample_rate);

	/* Define sinusoidal gyroscope motion parameters. */
	const float omega_dt =
		2.0f * NANO_PI * sample_interval_nanos * NANOS_TO_SEC;
	const float amplitude = MDEG_TO_RAD * 550.0f; /* [rad/sec] */

	bool calibration_received = false;

	gyro_cal_initialization_for_test(&gyro_cal);

	for (i = 0; i < (int)(20.0f * sample_rate); ++i) {
		const uint32_t timestamp_us =
			(i * sample_interval_nanos) / 1000;

		/* Generate and add an accelerometer sample. */
		gyro_cal_update_accel(
			&gyro_cal, timestamp_us,
			normal_random2(accel_bias, accel_rms_noise),
			normal_random2(accel_bias, accel_rms_noise),
			normal_random2(accel_bias, accel_rms_noise));

		/* Generate and add a gyroscope sample. */
		gyro_cal_update_gyro(
			&gyro_cal, timestamp_us,
			normal_random2(gyro_bias, gyro_rms_noise) +
				amplitude * sin(2.0f * omega_dt * i),
			normal_random2(gyro_bias, gyro_rms_noise) -
				amplitude * sin(2.1f * omega_dt * i),
			normal_random2(gyro_bias, gyro_rms_noise) +
				amplitude * cos(4.3f * omega_dt * i),
			kDefaultTemperatureKelvin);

		/* Check for calibration update. Break after first one. */
		calibration_received = gyro_cal_new_bias_available(&gyro_cal);
		if (calibration_received)
			break;
	}

	/* Determine that NO calibration had occurred. */
	TEST_EQ(calibration_received, false, "%d");

	/* Make sure that the device was NOT classified as "still". */
	TEST_GT(1.0f, gyro_cal.stillness_confidence, "%f");

	return EC_SUCCESS;
}

/**
 * Tests that a shift in a stillness window mean does not trigger a calibration.
 *
 * @return EC_SUCCESS on success.
 */
static int test_gyro_cal_win_mean_shift(void)
{
	struct gyro_cal gyro_cal;
	int i;

	/* Statistics for simulated gyroscope data. */
	const float sample_rate = 400.0f; /* [Hz] */
	const float gyro_bias = MDEG_TO_RAD * 150.0f; /* [rad/sec] */
	const float gyro_bias_shift = MDEG_TO_RAD * 60.0f; /* [rad/sec] */
	const uint64_t sample_interval_nanos = HZ_TO_PERIOD_NANOS(sample_rate);

	/* Initialize the gyro calibration. */
	gyro_cal_initialization_for_test(&gyro_cal);

	/*
	 * Simulates 8 seconds of sensor data (no noise, just a gyro mean shift
	 * after 4 seconds).
	 * Assumptions: The max stillness period is 6 seconds, and the mean
	 * delta limit is 50mDPS. The mean shift should be detected and exceed
	 * the 50mDPS limit, and no calibration should be triggered. NOTE: This
	 * step is not large enough to trip the variance checking within the
	 * stillness detectors.
	 */
	for (i = 0; i < (int)(8.0f * sample_rate); i++) {
		const uint32_t timestamp_us =
			(i * sample_interval_nanos) / 1000;

		/* Generate and add a accelerometer sample. */
		gyro_cal_update_accel(&gyro_cal, timestamp_us, 0.0f, 0.0f,
				      9.81f);

		/* Generate and add a gyroscope sample. */
		if (timestamp_us > 4 * SECOND) {
			gyro_cal_update_gyro(&gyro_cal, timestamp_us,
					     gyro_bias + gyro_bias_shift,
					     gyro_bias + gyro_bias_shift,
					     gyro_bias + gyro_bias_shift,
					     kDefaultTemperatureKelvin);
		} else {
			gyro_cal_update_gyro(&gyro_cal, timestamp_us, gyro_bias,
					     gyro_bias, gyro_bias,
					     kDefaultTemperatureKelvin);
		}
	}

	/* Determine that NO calibration had occurred. */
	TEST_EQ(gyro_cal_new_bias_available(&gyro_cal), false, "%d");

	return EC_SUCCESS;
}

/**
 * Tests that a temperature variation outside the acceptable range prevents a
 * calibration.
 *
 * @return EC_SUCCESS on success.
 */
static int test_gyro_cal_temperature_shift(void)
{
	int i;
	struct gyro_cal gyro_cal;

	/* Statistics for simulated gyroscope data. */
	const float sample_rate = 400.0f; /* [Hz] */
	const float gyro_bias = MDEG_TO_RAD * 150.0f; /* [rad/sec] */
	const float temperature_shift_kelvin = 2.6f;
	const uint64_t sample_interval_nanos = HZ_TO_PERIOD_NANOS(sample_rate);

	gyro_cal_initialization_for_test(&gyro_cal);

	/*
	 * Simulates 8 seconds of sensor data (no noise, just a temperature
	 * shift after 4 seconds).
	 * Assumptions: The max stillness period is 6 seconds, and the
	 * temperature delta limit is 1.5C. The shift should be detected and
	 * exceed the limit, and no calibration should be triggered.
	 */
	for (i = 0; i < (int)(8.0f * sample_rate); i++) {
		const uint32_t timestamp_us =
			(i * sample_interval_nanos) / 1000;
		float temperature_kelvin = kDefaultTemperatureKelvin;

		/* Generate and add a accelerometer sample. */
		gyro_cal_update_accel(&gyro_cal, timestamp_us, 0.0f, 0.0f,
				      9.81f);

		/* Sets the temperature value. */
		if (timestamp_us > 4 * SECOND)
			temperature_kelvin += temperature_shift_kelvin;

		/* Generate and add a gyroscope sample. */
		gyro_cal_update_gyro(&gyro_cal, timestamp_us, gyro_bias,
				     gyro_bias, gyro_bias,
				     (int)temperature_kelvin);
	}

	/* Determine that NO calibration had occurred. */
	TEST_EQ(gyro_cal_new_bias_available(&gyro_cal), false, "%d");

	return EC_SUCCESS;
}

/**
 * Verifies that complete sensor stillness results in the correct bias estimate
 * and produces the correct timestamp.
 *
 * @return EC_SUCCESS on success;
 */
static int test_gyro_cal_stillness_timestamp(void)
{
	struct gyro_cal gyro_cal;
	int64_t time_us;

	/*
	 * 10Hz update rate for 11 seconds should trigger the in-situ
	 * algorithms.
	 */
	const float gyro_bias_x = 0.09f;
	const float gyro_bias_y = -0.04f;
	const float gyro_bias_z = 0.05f;

	float bias[3];
	int temperature_kelvin = 273;
	uint32_t calibration_time_us = 0;

	gyro_cal_initialization_for_test(&gyro_cal);
	for (time_us = 0; time_us < 11 * SECOND; time_us += 100 * MSEC) {
		/* Generate and add a accelerometer sample. */
		gyro_cal_update_accel(&gyro_cal, time_us, 0.0f, 0.0f, 9.81f);

		/* Generate and add a gyroscope sample. */
		gyro_cal_update_gyro(&gyro_cal, time_us, gyro_bias_x,
				     gyro_bias_y, gyro_bias_z,
				     kDefaultTemperatureKelvin);
	}

	/* Determine if there is a new calibration. Get the calibration value.
	 */
	TEST_EQ(gyro_cal_new_bias_available(&gyro_cal), 1, "%d");

	gyro_cal_get_bias(&gyro_cal, bias, &temperature_kelvin,
			  &calibration_time_us);

	/* Make sure that the bias estimate is within kToleranceGyroRps. */
	TEST_NEAR(gyro_bias_x - bias[0], 0.0f, 0.0001f, "%f");
	TEST_NEAR(gyro_bias_y - bias[1], 0.0f, 0.0001f, "%f");
	TEST_NEAR(gyro_bias_z - bias[2], 0.0f, 0.0001f, "%f");

	/* Checks that the calibration occurred at the expected time. */
	TEST_EQ(6 * SECOND, gyro_cal.calibration_time_us, "%u");

	/* Make sure that the device was classified as 100% "still". */
	TEST_NEAR(1.0f, gyro_cal.stillness_confidence, 0.0001f, "%f");

	/* Make sure that the calibration temperature is correct. */
	TEST_EQ(kDefaultTemperatureKelvin, temperature_kelvin, "%d");

	return EC_SUCCESS;
}

/**
 * Verifies that setting an initial bias works.
 *
 * @return EC_SUCCESS on success.
 */
static int test_gyro_cal_set_bias(void)
{
	struct gyro_cal gyro_cal;

	/* Get the initialized bias value; should be zero. */
	float bias[3] = { 0.0f, 0.0f, 0.0f };
	int temperature_kelvin = 273;
	uint32_t calibration_time_us = 10;

	/* Initialize the gyro calibration. */
	gyro_cal_initialization_for_test(&gyro_cal);
	gyro_cal_get_bias(&gyro_cal, bias, &temperature_kelvin,
			  &calibration_time_us);
	TEST_NEAR(0.0f, bias[0], 0.0001f, "%f");
	TEST_NEAR(0.0f, bias[1], 0.0001f, "%f");
	TEST_NEAR(0.0f, bias[2], 0.0001f, "%f");
	TEST_EQ(0, temperature_kelvin, "%d");
	TEST_EQ(0, calibration_time_us, "%d");

	/* Set the calibration bias estimate. */
	bias[0] = 1.0f;
	bias[1] = 2.0f;
	bias[2] = 3.0f;
	gyro_cal_set_bias(&gyro_cal, bias, 31, 3 * 60 * SECOND);

	bias[0] = 0.0f;
	bias[1] = 0.0f;
	bias[2] = 0.0f;
	/* Check that it was set correctly. */
	gyro_cal_get_bias(&gyro_cal, bias, &temperature_kelvin,
			  &calibration_time_us);
	TEST_NEAR(1.0f, bias[0], 0.0001f, "%f");
	TEST_NEAR(2.0f, bias[1], 0.0001f, "%f");
	TEST_NEAR(3.0f, bias[2], 0.0001f, "%f");
	TEST_EQ(31, temperature_kelvin, "%d");
	TEST_EQ(3 * 60 * SECOND, calibration_time_us, "%u");

	return EC_SUCCESS;
}

/**
 * Verifies that the gyroCalRemoveBias function works as intended.
 *
 * @return EC_SUCCESS on success
 */
static int test_gyro_cal_remove_bias(void)
{
	struct gyro_cal gyro_cal;
	float bias[3] = { 1.0f, 2.0f, 3.0f };
	float bias_out[3];

	/* Initialize the gyro calibration. */
	gyro_cal_initialization_for_test(&gyro_cal);

	/* Set an calibration bias estimate. */
	gyro_cal_set_bias(&gyro_cal, bias, kDefaultTemperatureKelvin,
			  5 * 60 * SECOND);

	/* Correct the bias, and check that it has been adequately removed. */
	gyro_cal_remove_bias(&gyro_cal, bias, bias_out);

	/* Make sure that the bias estimate is within kToleranceGyroRps. */
	TEST_NEAR(0.0f, bias_out[0], 0.0001f, "%f");
	TEST_NEAR(0.0f, bias_out[1], 0.0001f, "%f");
	TEST_NEAR(0.0f, bias_out[2], 0.0001f, "%f");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_gyro_cal_calibration);
	RUN_TEST(test_gyro_cal_no_calibration);
	RUN_TEST(test_gyro_cal_win_mean_shift);
	RUN_TEST(test_gyro_cal_temperature_shift);
	RUN_TEST(test_gyro_cal_stillness_timestamp);
	RUN_TEST(test_gyro_cal_set_bias);
	RUN_TEST(test_gyro_cal_remove_bias);

	test_print_result();
}

/* Mock out mkbp_send_event. Rarely, but occasionally, mkbp_send_event gets
 * called and the coverage is thrown off.
 */
int mkbp_send_event(uint8_t event_type)
{
	return 1;
}
