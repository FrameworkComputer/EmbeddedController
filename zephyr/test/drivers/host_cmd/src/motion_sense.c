/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "console.h"
#include "driver/accel_bma2x2.h"
#include "hooks.h"
#include "lid_angle.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "motion_sense_fifo.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/fff.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

static int16_t mock_offset[3];

FAKE_VALUE_FUNC(int, mock_set_range, struct motion_sensor_t *, int, int);
FAKE_VALUE_FUNC(int, mock_set_offset, const struct motion_sensor_t *,
		const int16_t *, int16_t);
FAKE_VALUE_FUNC(int, mock_get_offset, const struct motion_sensor_t *, int16_t *,
		int16_t *);
static int mock_get_offset_custom(const struct motion_sensor_t *s,
				  int16_t *offset, int16_t *temp)
{
	for (int i = 0; i < 3; i++) {
		offset[i] = mock_offset[i];
	}

	return mock_get_offset_fake.return_val;
}

FAKE_VALUE_FUNC(int, mock_set_scale, const struct motion_sensor_t *,
		const uint16_t *, int16_t);
FAKE_VALUE_FUNC(int, mock_get_scale, const struct motion_sensor_t *, uint16_t *,
		int16_t *);
FAKE_VALUE_FUNC(int, mock_perform_calib, struct motion_sensor_t *, int);

/**
 * Get the size needed for a struct ec_response_motion_sense
 */
#define RESPONSE_MOTION_SENSE_BUFFER_SIZE(n)       \
	(sizeof(struct ec_response_motion_sense) + \
	 n * sizeof(struct ec_response_motion_sensor_data))

#define RESPONSE_SENSOR_FIFO_SIZE(n) \
	(sizeof(struct ec_response_motion_sense) + n * sizeof(uint16_t))

struct host_cmd_motion_sense_fixture {
	const struct accelgyro_drv *sensor_0_drv;
	struct accelgyro_drv mock_drv;
};

static void *host_cmd_motion_sense_setup(void)
{
	static struct host_cmd_motion_sense_fixture fixture = {
		.mock_drv = {
			.set_range = mock_set_range,
			.set_offset = mock_set_offset,
			.get_offset = mock_get_offset,
			.set_scale = mock_set_scale,
			.get_scale = mock_get_scale,
			.perform_calib = mock_perform_calib,
		},
	};

	fixture.sensor_0_drv = motion_sensors[0].drv;

	return &fixture;
}

static void host_cmd_motion_sense_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(mock_set_range);
	RESET_FAKE(mock_set_offset);
	RESET_FAKE(mock_get_offset);
	RESET_FAKE(mock_set_scale);
	RESET_FAKE(mock_get_scale);
	RESET_FAKE(mock_perform_calib);
	FFF_RESET_HISTORY();

	/* Setup proxy functions */
	mock_get_offset_fake.custom_fake = mock_get_offset_custom;

	motion_sensors[0].config[SENSOR_CONFIG_AP].odr = 0;
	motion_sensors[0].config[SENSOR_CONFIG_AP].ec_rate = 1000 * MSEC;
	zassert_ok(shell_execute_cmd(get_ec_shell(), "accelinit 0"));
	task_wake(TASK_ID_MOTIONSENSE);
	k_sleep(K_MSEC(100));

	atomic_clear(&motion_sensors[0].flush_pending);

	/* Reset the lid wake angle to 0 degrees. */
	lid_angle_set_wake_angle(0);
}

static void host_cmd_motion_sense_after(void *fixture)
{
	struct host_cmd_motion_sense_fixture *this = fixture;
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = this->sensor_0_drv;
	host_cmd_motion_sense_int_enable(0, &response);
	motion_sensors[0].flags &= ~MOTIONSENSE_FLAG_IN_SPOOF_MODE;
	motion_sensors[0].config[SENSOR_CONFIG_AP].odr = 0;
	motion_sensors[0].config[SENSOR_CONFIG_AP].ec_rate = 1000 * MSEC;
}

ZTEST_SUITE(host_cmd_motion_sense, drivers_predicate_post_main,
	    host_cmd_motion_sense_setup, host_cmd_motion_sense_before,
	    host_cmd_motion_sense_after, NULL);

ZTEST_USER(host_cmd_motion_sense, test_dump)
{
	uint8_t response_buffer[RESPONSE_MOTION_SENSE_BUFFER_SIZE(
		ALL_MOTION_SENSORS)];
	struct ec_response_motion_sense *result =
		(struct ec_response_motion_sense *)response_buffer;

	/* Set up the motion sensor data */
	for (int i = 0; i < ALL_MOTION_SENSORS; ++i) {
		motion_sensors[i].xyz[0] = i;
		motion_sensors[i].xyz[1] = i + 1;
		motion_sensors[i].xyz[2] = i + 2;
	}

	/* Make sure that the accelerometer status presence bit is off */
	*host_get_memmap(EC_MEMMAP_ACC_STATUS) &=
		~(EC_MEMMAP_ACC_STATUS_PRESENCE_BIT);

	/* Dump all the sensors info */
	host_cmd_motion_sense_dump(ALL_MOTION_SENSORS, result,
				   sizeof(response_buffer));

	zassert_equal(result->dump.module_flags, 0);
	zassert_equal(result->dump.sensor_count, ALL_MOTION_SENSORS);

	/*
	 * Test the values returned in the dump. Normally we shouldn't be doing
	 * tests in a loop, but since the number of sensors (as well as the
	 * order) is adjustable by devicetree, it would be too difficult to hard
	 * code here.
	 * When CONFIG_GESTURE_HOST_DETECTION is enabled, ALL_MOTION_SENSORS is
	 * increased by 1 (see include/motion_sense.h). Additionally,
	 * host_cmd_motion_sense() only fills in |motion_sensor_count| worth of
	 * data (not ALL_MOTION_SENSORS+1), and zeroes out the rest, so only
	 * validate |motion_sensor_count| worth of data and that the rest is
	 * zeroed out.
	 */
	for (int i = 0; i < ALL_MOTION_SENSORS; ++i) {
		if (i < motion_sensor_count) {
			zassert_equal(result->dump.sensor[i].flags,
				      MOTIONSENSE_SENSOR_FLAG_PRESENT, NULL);
			zassert_equal(result->dump.sensor[i].data[0], i);
			zassert_equal(result->dump.sensor[i].data[1], i + 1);
			zassert_equal(result->dump.sensor[i].data[2], i + 2);
		} else {
			zassert_equal(result->dump.sensor[i].data[0], 0);
			zassert_equal(result->dump.sensor[i].data[1], 0);
			zassert_equal(result->dump.sensor[i].data[2], 0);
		}
	}

	/* Make sure that the accelerometer status presence bit is on */
	*host_get_memmap(EC_MEMMAP_ACC_STATUS) |=
		EC_MEMMAP_ACC_STATUS_PRESENCE_BIT;

	/* Dump all the sensors info */
	host_cmd_motion_sense_dump(ALL_MOTION_SENSORS, result,
				   sizeof(response_buffer));

	zassert_equal(result->dump.module_flags, MOTIONSENSE_MODULE_FLAG_ACTIVE,
		      NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_dump__large_max_sensor_count)
{
	uint8_t response_buffer[RESPONSE_MOTION_SENSE_BUFFER_SIZE(
		ALL_MOTION_SENSORS)];
	struct ec_response_motion_sense *result =
		(struct ec_response_motion_sense *)response_buffer;

	host_cmd_motion_sense_dump(ALL_MOTION_SENSORS + 1, result,
				   sizeof(response_buffer));

	zassert_equal(result->dump.sensor_count, ALL_MOTION_SENSORS);
}

ZTEST_USER(host_cmd_motion_sense, test_read_data__invalid_sensor_num)
{
	struct ec_response_motion_sense response;

	zassert_equal(host_cmd_motion_sense_data(UINT8_MAX, &response),
		      EC_RES_INVALID_PARAM, NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_read_data)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].xyz[0] = 1;
	motion_sensors[0].xyz[1] = 2;
	motion_sensors[0].xyz[2] = 3;

	zassert_ok(host_cmd_motion_sense_data(0, &response));
	zassert_equal(response.data.flags, 0);
	zassert_equal(response.data.data[0], 1);
	zassert_equal(response.data.data[1], 2);
	zassert_equal(response.data.data[2], 3);
}

ZTEST_USER(host_cmd_motion_sense, test_get_info__invalid_sensor_num)
{
	struct ec_response_motion_sense response;

	zassert_equal(host_cmd_motion_sense_info(/*cmd_version=*/1,
						 /*sensor_num=*/UINT8_MAX,
						 &response),
		      EC_RES_INVALID_PARAM, NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_get_info_v1)
{
	struct ec_response_motion_sense response;

	zassert_ok(host_cmd_motion_sense_info(/*cmd_version=*/1,
					      /*sensor_num=*/0, &response),
		   NULL);
	zassert_equal(response.info.type, motion_sensors[0].type);
	zassert_equal(response.info.location, motion_sensors[0].location);
	zassert_equal(response.info.chip, motion_sensors[0].chip);
}

ZTEST_USER(host_cmd_motion_sense, test_get_info_v3)
{
	struct ec_response_motion_sense response;

	zassert_ok(host_cmd_motion_sense_info(/*cmd_version=*/3,
					      /*sensor_num=*/0, &response),
		   NULL);
	zassert_equal(response.info.type, motion_sensors[0].type);
	zassert_equal(response.info.location, motion_sensors[0].location);
	zassert_equal(response.info.chip, motion_sensors[0].chip);
	zassert_equal(response.info_3.min_frequency,
		      motion_sensors[0].min_frequency, NULL);
	zassert_equal(response.info_3.max_frequency,
		      motion_sensors[0].max_frequency, NULL);
	zassert_equal(response.info_3.fifo_max_event_count,
		      CONFIG_ACCEL_FIFO_SIZE, NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_get_info_v4__no_read_temp)
{
	struct ec_response_motion_sense response;

	zassert_ok(host_cmd_motion_sense_info(/*cmd_version=*/4,
					      /*sensor_num=*/0, &response),
		   NULL);
	zassert_equal(response.info.type, motion_sensors[0].type);
	zassert_equal(response.info.location, motion_sensors[0].location);
	zassert_equal(response.info.chip, motion_sensors[0].chip);
	if (IS_ENABLED(CONFIG_ONLINE_CALIB)) {
		zassert_true(response.info_4.flags &
				     MOTION_SENSE_CMD_INFO_FLAG_ONLINE_CALIB,
			     NULL);
	} else {
		zassert_false(response.info_4.flags &
				      MOTION_SENSE_CMD_INFO_FLAG_ONLINE_CALIB,
			      NULL);
	}
}

ZTEST_USER(host_cmd_motion_sense, test_get_ec_rate__invalid_sensor_num)
{
	struct ec_response_motion_sense response;

	zassert_equal(host_cmd_motion_sense_ec_rate(
			      /*sensor_num=*/0xff,
			      /*data_rate_ms=*/EC_MOTION_SENSE_NO_VALUE,
			      &response),
		      EC_RES_INVALID_PARAM, NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_get_ec_rate)
{
	struct ec_response_motion_sense response;

	/* Set the power level to S3, the default config from device-tree is for
	 * 100ms
	 */
	test_set_chipset_to_power_level(POWER_S3);
	zassert_ok(host_cmd_motion_sense_ec_rate(
			   /*sensor_num=*/0,
			   /*data_rate_ms=*/EC_MOTION_SENSE_NO_VALUE,
			   &response),
		   NULL);
	zassert_equal(response.ec_rate.ret, 1000);
}

ZTEST_USER(host_cmd_motion_sense, test_set_ec_rate)
{
	struct ec_response_motion_sense response;

	/* Set the power level to S3, the default config from device-tree is for
	 * 100ms
	 */
	test_set_chipset_to_power_level(POWER_S3);
	zassert_ok(host_cmd_motion_sense_ec_rate(
			   /*sensor_num=*/0, /*data_rate_ms=*/2000, &response),
		   NULL);
	/* The command should return the new rate */
	zassert_equal(response.ec_rate.ret, 2000, "Expected 2000, but got %d",
		      response.ec_rate.ret);
	/* The sensor's AP config value should be updated */
	zassert_equal(motion_sensors[0].config[SENSOR_CONFIG_AP].ec_rate,
		      2000 * MSEC, NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_odr_invalid_sensor_num)
{
	struct ec_response_motion_sense response;

	zassert_equal(EC_RES_INVALID_PARAM,
		      host_cmd_motion_sense_odr(
			      /*sensor_num=*/0xff,
			      /*odr=*/EC_MOTION_SENSE_NO_VALUE,
			      /*round_up=*/false, &response),
		      NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_odr_get)
{
	struct ec_response_motion_sense response;

	zassert_ok(motion_sensors[0].drv->set_data_rate(&motion_sensors[0],
							1000000, false),
		   NULL);
	zassert_ok(host_cmd_motion_sense_odr(/*sensor_num=*/0,
					     /*odr=*/EC_MOTION_SENSE_NO_VALUE,
					     /*round_up=*/false, &response),
		   NULL);
	zassert_equal(BMA2x2_REG_TO_BW(BMA2x2_BW_1000HZ),
		      response.sensor_odr.ret, "Expected %d, but got %d",
		      BMA2x2_REG_TO_BW(BMA2x2_BW_1000HZ),
		      response.sensor_odr.ret);
}

ZTEST_USER(host_cmd_motion_sense, test_odr_set)
{
	struct ec_response_motion_sense response;

	zassert_ok(motion_sensors[0].drv->set_data_rate(&motion_sensors[0], 0,
							false),
		   NULL);
	zassert_ok(host_cmd_motion_sense_odr(/*sensor_num=*/0,
					     /*odr=*/1000000,
					     /*round_up=*/true, &response),
		   NULL);
	/* Check the set value */
	zassert_equal(1000000 | ROUND_UP_FLAG,
		      motion_sensors[0].config[SENSOR_CONFIG_AP].odr,
		      "Expected %d, but got %d", 1000000 | ROUND_UP_FLAG,
		      motion_sensors[0].config[SENSOR_CONFIG_AP].odr);
	/* Check the returned value */
	zassert_equal(BMA2x2_REG_TO_BW(BMA2x2_BW_7_81HZ),
		      response.sensor_odr.ret, "Expected %d, but got %d",
		      BMA2x2_REG_TO_BW(BMA2x2_BW_7_81HZ),
		      response.sensor_odr.ret);
}

ZTEST_USER(host_cmd_motion_sense, test_odr_set_suspend)
{
	int i;
	struct ec_response_motion_sense response;

	/* This test requires there is at least one sensor has
	 * active_mask set to SENSOR_ACTIVE_S0
	 */
	for (i = 0; i < motion_sensor_count; i++) {
		if (motion_sensors[i].active_mask == SENSOR_ACTIVE_S0)
			break;
	}

	zassert_true(i < motion_sensor_count,
		     "No sensor has SENSOR_ACTIVE_S0 set");

	zassert_ok(motion_sensors[i].drv->set_data_rate(&motion_sensors[i], 0,
							false),
		   NULL);
	zassert_ok(host_cmd_motion_sense_odr(/*sensor_num=*/i,
					     /*odr=*/10000,
					     /*round_up=*/true, &response),
		   NULL);

	/* Check the set value */
	zassert_equal(10000 | ROUND_UP_FLAG,
		      motion_sensors[i].config[SENSOR_CONFIG_AP].odr,
		      "Expected %d, but got %d", 10000 | ROUND_UP_FLAG,
		      motion_sensors[i].config[SENSOR_CONFIG_AP].odr);

	hook_notify(HOOK_CHIPSET_SUSPEND);
	/* System enter suspend then resume */
	k_sleep(K_SECONDS(2));
	zassert_equal(0,
		      motion_sensors[i].drv->get_data_rate(&motion_sensors[i]),
		      "%s expected %d, but got %d", 0, motion_sensors[i].name,
		      motion_sensors[i].drv->get_data_rate(&motion_sensors[i]));
	k_sleep(K_SECONDS(2));
	hook_notify(HOOK_CHIPSET_RESUME);
}

ZTEST_USER(host_cmd_motion_sense, test_range_invalid_sensor_num)
{
	struct ec_response_motion_sense response;

	zassert_equal(EC_RES_INVALID_PARAM,
		      host_cmd_motion_sense_range(
			      /*sensor_num=*/0xff,
			      /*range=*/EC_MOTION_SENSE_NO_VALUE,
			      /*round_up=*/false, &response),
		      NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_get_range)
{
	struct ec_response_motion_sense response;

	zassert_ok(host_cmd_motion_sense_range(
			   /*sensor_num=*/0, /*range=*/EC_MOTION_SENSE_NO_VALUE,
			   /*round_up=*/false, &response),
		   NULL);
	zassert_equal(motion_sensors[0].current_range,
		      response.sensor_range.ret, "Expected %d, but got %d",
		      motion_sensors[0].current_range,
		      response.sensor_range.ret);
}

ZTEST_USER(host_cmd_motion_sense, test_null_set_range_in_driver)
{
	struct ec_response_motion_sense response;
	struct accelgyro_drv drv = { 0 };

	motion_sensors[0].drv = &drv;
	zassert_equal(EC_RES_INVALID_COMMAND,
		      host_cmd_motion_sense_range(/*sensor_num=*/0, /*range=*/4,
						  /*round_up=*/false,
						  &response),
		      NULL);
}

ZTEST_USER_F(host_cmd_motion_sense, test_set_range_error)
{
	struct ec_response_motion_sense response;

	mock_set_range_fake.return_val = 1;
	motion_sensors[0].drv = &fixture->mock_drv;

	zassert_equal(EC_RES_INVALID_PARAM,
		      host_cmd_motion_sense_range(/*sensor_num=*/0, /*range=*/4,
						  /*round_up=*/false,
						  &response),
		      NULL);
	zassert_equal(1, mock_set_range_fake.call_count);
}

ZTEST_USER_F(host_cmd_motion_sense, test_set_range)
{
	struct ec_response_motion_sense response;

	mock_set_range_fake.return_val = 0;
	motion_sensors[0].drv = &fixture->mock_drv;

	zassert_ok(host_cmd_motion_sense_range(/*sensor_num=*/0, /*range=*/4,
					       /*round_up=*/false, &response),
		   NULL);
	zassert_equal(1, mock_set_range_fake.call_count);
	zassert_equal(4, mock_set_range_fake.arg1_history[0]);
	zassert_equal(0, mock_set_range_fake.arg2_history[0]);
}

ZTEST_USER(host_cmd_motion_sense, test_offset_invalid_sensor_num)
{
	struct ec_response_motion_sense response;

	zassert_equal(EC_RES_INVALID_PARAM,
		      host_cmd_motion_sense_offset(
			      /*sensor_num=*/0xff, /*flags=*/0,
			      /*temperature=*/0, /*offset_x=*/0,
			      /*offset_y=*/0, /*offset_z=*/0, &response),
		      NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_offset_missing_get_offset_in_driver)
{
	struct ec_response_motion_sense response;
	struct accelgyro_drv drv = { 0 };

	motion_sensors[0].drv = &drv;

	zassert_equal(EC_RES_INVALID_COMMAND,
		      host_cmd_motion_sense_offset(
			      /*sensor_num=*/0, /*flags=*/0,
			      /*temperature=*/0, /*offset_x=*/0,
			      /*offset_y=*/0, /*offset_z=*/0, &response),
		      NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_offset_missing_set_offset_in_driver)
{
	struct ec_response_motion_sense response;
	struct accelgyro_drv drv = { 0 };

	motion_sensors[0].drv = &drv;

	zassert_equal(EC_RES_INVALID_COMMAND,
		      host_cmd_motion_sense_offset(
			      /*sensor_num=*/0,
			      /*flags=*/MOTION_SENSE_SET_OFFSET,
			      /*temperature=*/0, /*offset_x=*/0,
			      /*offset_y=*/0, /*offset_z=*/0, &response),
		      NULL);
}

ZTEST_USER_F(host_cmd_motion_sense, test_offset_fail_to_set)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &fixture->mock_drv;
	mock_set_offset_fake.return_val = EC_RES_ERROR;

	zassert_equal(EC_RES_ERROR,
		      host_cmd_motion_sense_offset(
			      /*sensor_num=*/0,
			      /*flags=*/MOTION_SENSE_SET_OFFSET,
			      /*temperature=*/0, /*offset_x=*/0,
			      /*offset_y=*/0, /*offset_z=*/0, &response),
		      NULL);
	zassert_equal(1, mock_set_offset_fake.call_count);
}

ZTEST_USER_F(host_cmd_motion_sense, test_offset_fail_to_get)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &fixture->mock_drv;
	mock_set_offset_fake.return_val = EC_RES_SUCCESS;
	mock_get_offset_fake.return_val = EC_RES_ERROR;

	zassert_equal(EC_RES_ERROR,
		      host_cmd_motion_sense_offset(
			      /*sensor_num=*/0,
			      /*flags=*/MOTION_SENSE_SET_OFFSET,
			      /*temperature=*/0, /*offset_x=*/0,
			      /*offset_y=*/0, /*offset_z=*/0, &response),
		      NULL);
	zassert_equal(1, mock_set_offset_fake.call_count);
	zassert_equal(1, mock_get_offset_fake.call_count);
}

ZTEST_USER_F(host_cmd_motion_sense, test_get_offset)
{
	struct ec_response_motion_sense response;

	mock_offset[0] = 0xaa;
	mock_offset[1] = 0xbb;
	mock_offset[2] = 0xcc;

	motion_sensors[0].drv = &fixture->mock_drv;
	mock_get_offset_fake.return_val = EC_RES_SUCCESS;
	mock_set_offset_fake.return_val = EC_RES_SUCCESS;

	zassert_ok(host_cmd_motion_sense_offset(
			   /*sensor_num=*/0,
			   /*flags=*/MOTION_SENSE_SET_OFFSET,
			   /*temperature=*/1, /*offset_x=*/2,
			   /*offset_y=*/3, /*offset_z=*/4, &response),
		   NULL);
	zassert_equal(1, mock_set_offset_fake.call_count);
	zassert_equal(1, mock_get_offset_fake.call_count);
	for (int i = 0; i < ARRAY_SIZE(response.sensor_offset.offset); i++) {
		zassert_equal(response.sensor_offset.offset[i], mock_offset[i],
			      NULL);
	}
	zassert_equal(1, mock_set_offset_fake.arg2_history[0]);
}

ZTEST_USER(host_cmd_motion_sense, test_scale_invalid_sensor_num)
{
	struct ec_response_motion_sense response;

	zassert_equal(EC_RES_INVALID_PARAM,
		      host_cmd_motion_sense_scale(
			      /*sensor_num=*/0xff,
			      /*flags=*/0,
			      /*temperature=*/1, /*scale_x=*/2,
			      /*scale_y=*/3, /*scale_z=*/4, &response),
		      NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_get_scale_not_in_driver)
{
	struct ec_response_motion_sense response;
	struct accelgyro_drv drv = *motion_sensors[0].drv;

	drv.get_scale = NULL;
	motion_sensors[0].drv = &drv;

	zassert_equal(EC_RES_INVALID_COMMAND,
		      host_cmd_motion_sense_scale(
			      /*sensor_num=*/0,
			      /*flags=*/0,
			      /*temperature=*/1, /*scale_x=*/2,
			      /*scale_y=*/3, /*scale_z=*/4, &response),
		      NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_set_scale_not_in_driver)
{
	struct ec_response_motion_sense response;
	struct accelgyro_drv drv = *motion_sensors[0].drv;

	drv.set_scale = NULL;
	motion_sensors[0].drv = &drv;

	zassert_equal(EC_RES_INVALID_COMMAND,
		      host_cmd_motion_sense_scale(
			      /*sensor_num=*/0,
			      /*flags=*/MOTION_SENSE_SET_OFFSET,
			      /*temperature=*/1, /*scale_x=*/2,
			      /*scale_y=*/3, /*scale_z=*/4, &response),
		      NULL);
}

ZTEST_USER_F(host_cmd_motion_sense, test_get_scale_fail)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &fixture->mock_drv;
	mock_get_scale_fake.return_val = 1;

	zassert_equal(1,
		      host_cmd_motion_sense_scale(
			      /*sensor_num=*/0,
			      /*flags=*/0,
			      /*temperature=*/1, /*scale_x=*/2,
			      /*scale_y=*/3, /*scale_z=*/4, &response),
		      NULL);
	zassert_equal(1, mock_get_scale_fake.call_count);
}

ZTEST_USER_F(host_cmd_motion_sense, test_set_scale_fail)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &fixture->mock_drv;
	mock_set_scale_fake.return_val = 1;

	zassert_equal(1,
		      host_cmd_motion_sense_scale(
			      /*sensor_num=*/0,
			      /*flags=*/MOTION_SENSE_SET_OFFSET,
			      /*temperature=*/1, /*scale_x=*/2,
			      /*scale_y=*/3, /*scale_z=*/4, &response),
		      NULL);
	zassert_equal(1, mock_set_scale_fake.call_count);
}

ZTEST_USER_F(host_cmd_motion_sense, test_set_get_scale)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &fixture->mock_drv;
	mock_set_scale_fake.return_val = 0;
	mock_get_scale_fake.return_val = 0;

	zassert_ok(host_cmd_motion_sense_scale(
			   /*sensor_num=*/0,
			   /*flags=*/MOTION_SENSE_SET_OFFSET,
			   /*temperature=*/1, /*scale_x=*/2,
			   /*scale_y=*/3, /*scale_z=*/4, &response),
		   NULL);
	zassert_equal(1, mock_set_scale_fake.call_count);
	zassert_equal(1, mock_get_scale_fake.call_count);
	zassert_equal(1, mock_set_scale_fake.arg2_history[0]);
}

ZTEST_USER(host_cmd_motion_sense, test_calib_invalid_sensor_num)
{
	struct ec_response_motion_sense response;

	zassert_equal(EC_RES_INVALID_PARAM,
		      host_cmd_motion_sense_calib(/*sensor_num=*/0xff,
						  /*enable=*/false, &response),
		      NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_calib_not_in_driver)
{
	struct ec_response_motion_sense response;
	struct accelgyro_drv drv = { 0 };

	motion_sensors[0].drv = &drv;
	zassert_equal(EC_RES_INVALID_COMMAND,
		      host_cmd_motion_sense_calib(/*sensor_num=*/0,
						  /*enable=*/false, &response),
		      NULL);
}

ZTEST_USER_F(host_cmd_motion_sense, test_calib_fail)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &fixture->mock_drv;
	mock_perform_calib_fake.return_val = 1;

	zassert_equal(1,
		      host_cmd_motion_sense_calib(/*sensor_num=*/0,
						  /*enable=*/false, &response),
		      NULL);
	zassert_equal(1, mock_perform_calib_fake.call_count);
	zassert_false(mock_perform_calib_fake.arg1_history[0]);
}

ZTEST_USER_F(host_cmd_motion_sense, test_calib_success__fail_get_offset)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &fixture->mock_drv;
	mock_perform_calib_fake.return_val = 0;
	mock_get_offset_fake.return_val = 1;

	zassert_equal(1,
		      host_cmd_motion_sense_calib(/*sensor_num=*/0,
						  /*enable=*/false, &response),
		      NULL);
	zassert_equal(1, mock_perform_calib_fake.call_count);
	zassert_equal(1, mock_get_offset_fake.call_count);
	zassert_false(mock_perform_calib_fake.arg1_history[0]);
}

ZTEST_USER_F(host_cmd_motion_sense, test_calib)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &fixture->mock_drv;
	mock_perform_calib_fake.return_val = 0;
	mock_get_offset_fake.return_val = 0;
	zassert_equal(motion_sensors[0].state, SENSOR_READY);

	zassert_ok(host_cmd_motion_sense_calib(/*sensor_num=*/0,
					       /*enable=*/true, &response),
		   NULL);
	zassert_equal(1, mock_perform_calib_fake.call_count);
	zassert_equal(1, mock_get_offset_fake.call_count);
	zassert_true(mock_perform_calib_fake.arg1_history[0]);
}

ZTEST(host_cmd_motion_sense, test_fifo_flush__invalid_sensor_num)
{
	int rv;
	struct ec_response_motion_sense response;

	rv = host_cmd_motion_sense_fifo_flush(/*sensor_num=*/0xff, &response,
					      sizeof(response));
	zassert_equal(rv, EC_RES_INVALID_PARAM);
}

ZTEST(host_cmd_motion_sense, test_fifo_flush)
{
	uint8_t response_buffer[RESPONSE_SENSOR_FIFO_SIZE(ALL_MOTION_SENSORS)];
	struct ec_response_motion_sense *response =
		(struct ec_response_motion_sense *)response_buffer;

	zassert_ok(host_cmd_motion_sense_fifo_flush(/*sensor_num=*/0, response,
						    sizeof(response_buffer)),
		   NULL);
	zassert_equal(1, motion_sensors[0].flush_pending);
}

ZTEST(host_cmd_motion_sense, test_fifo_info)
{
	uint8_t response_buffer[RESPONSE_SENSOR_FIFO_SIZE(ALL_MOTION_SENSORS)];
	struct ec_response_motion_sense *response =
		(struct ec_response_motion_sense *)response_buffer;

	zassert_ok(host_cmd_motion_sense_fifo_info(response,
						   sizeof(response_buffer)));
}

ZTEST(host_cmd_motion_sense, test_fifo_read)
{
	struct ec_response_motion_sensor_data data;
	uint8_t response_buffer[RESPONSE_MOTION_SENSE_BUFFER_SIZE(2)];
	struct ec_response_motion_sense *response =
		(struct ec_response_motion_sense *)response_buffer;

	motion_sensors[0].oversampling_ratio = 1;
	motion_sensors[1].oversampling_ratio = 1;

	data = (struct ec_response_motion_sensor_data){
		.flags = 0,
		.sensor_num = 0,
		.data = { 0, 1, 2 },
	};
	motion_sense_fifo_stage_data(&data, &motion_sensors[0], 1, 0);

	data = (struct ec_response_motion_sensor_data){
		.flags = 0,
		.sensor_num = 1,
		.data = { 3, 4, 5 },
	};
	motion_sense_fifo_stage_data(&data, &motion_sensors[1], 1, 5);
	motion_sense_fifo_commit_data();

	/* Remove the ODR change confirmation after init. */
	zassert_ok(host_cmd_motion_sense_fifo_read(4, response));
	zassert_equal(2, response->fifo_read.number_data);

	zassert_equal(MOTIONSENSE_SENSOR_FLAG_ODR |
			      MOTIONSENSE_SENSOR_FLAG_TIMESTAMP,
		      response->fifo_read.data[0].flags, NULL);
	zassert_equal(0, response->fifo_read.data[0].sensor_num);

	/* Remove the timestamp when the motion_sense task complete */
	zassert_equal(MOTIONSENSE_SENSOR_FLAG_TIMESTAMP,
		      response->fifo_read.data[1].flags, NULL);
	zassert_equal(0xff, response->fifo_read.data[1].sensor_num);

	/* Read 2 samples */
	zassert_ok(host_cmd_motion_sense_fifo_read(4, response));
	zassert_equal(2, response->fifo_read.number_data);

	zassert_equal(MOTIONSENSE_SENSOR_FLAG_TIMESTAMP,
		      response->fifo_read.data[0].flags, NULL);
	zassert_equal(0, response->fifo_read.data[0].sensor_num);
	/*
	 * The timestamp may be modified based on the previous timestamp from
	 * the task.
	 */

	zassert_equal(0, response->fifo_read.data[1].flags);
	zassert_equal(0, response->fifo_read.data[1].sensor_num);
	zassert_equal(0, response->fifo_read.data[1].data[0]);
	zassert_equal(1, response->fifo_read.data[1].data[1]);
	zassert_equal(2, response->fifo_read.data[1].data[2]);

	/* Read the next 2 samples */
	zassert_ok(host_cmd_motion_sense_fifo_read(4, response));
	zassert_equal(2, response->fifo_read.number_data);
	zassert_equal(MOTIONSENSE_SENSOR_FLAG_TIMESTAMP,
		      response->fifo_read.data[0].flags, NULL);
	zassert_equal(1, response->fifo_read.data[0].sensor_num);
	/*
	 * The timestamp may be modified based on the previous timestamp from
	 * the task.
	 */

	zassert_equal(0, response->fifo_read.data[1].flags);
	zassert_equal(1, response->fifo_read.data[1].sensor_num);
	zassert_equal(3, response->fifo_read.data[1].data[0]);
	zassert_equal(4, response->fifo_read.data[1].data[1]);
	zassert_equal(5, response->fifo_read.data[1].data[2]);
}

ZTEST(host_cmd_motion_sense, test_int_enable)
{
	struct ec_response_motion_sense response;

	zassert_equal(EC_RES_INVALID_PARAM,
		      host_cmd_motion_sense_int_enable(2, &response), NULL);

	/* Make sure we start off disabled */
	zassert_ok(host_cmd_motion_sense_int_enable(0, &response));

	/* Test enable */
	zassert_ok(host_cmd_motion_sense_int_enable(1, &response));
	zassert_ok(host_cmd_motion_sense_int_enable(EC_MOTION_SENSE_NO_VALUE,
						    &response),
		   NULL);
	zassert_equal(1, response.fifo_int_enable.ret);

	/* Test disable */
	zassert_ok(host_cmd_motion_sense_int_enable(0, &response));
	zassert_ok(host_cmd_motion_sense_int_enable(EC_MOTION_SENSE_NO_VALUE,
						    &response),
		   NULL);
	zassert_equal(0, response.fifo_int_enable.ret);
}

ZTEST(host_cmd_motion_sense, test_spoof_invalid_sensor_num)
{
	struct ec_response_motion_sense response;

	zassert_equal(EC_RES_INVALID_PARAM,
		      host_cmd_motion_sense_spoof(0xff, 0, 0, 0, 0, &response),
		      NULL);
}

ZTEST(host_cmd_motion_sense, test_spoof_disable)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].flags |= MOTIONSENSE_FLAG_IN_SPOOF_MODE;
	zassert_ok(host_cmd_motion_sense_spoof(0,
					       MOTIONSENSE_SPOOF_MODE_DISABLE,
					       0, 0, 0, &response),
		   NULL);
	zassert_equal(0,
		      motion_sensors[0].flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE,
		      NULL);

	zassert_ok(host_cmd_motion_sense_spoof(0, MOTIONSENSE_SPOOF_MODE_QUERY,
					       0, 0, 0, &response),
		   NULL);
	zassert_false(response.spoof.ret);
}

ZTEST(host_cmd_motion_sense, test_spoof_custom)
{
	struct ec_response_motion_sense response;

	zassert_ok(host_cmd_motion_sense_spoof(0, MOTIONSENSE_SPOOF_MODE_CUSTOM,
					       -8, 16, -32, &response),
		   NULL);
	zassert_equal(MOTIONSENSE_FLAG_IN_SPOOF_MODE,
		      motion_sensors[0].flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE,
		      NULL);
	zassert_equal(-8, motion_sensors[0].spoof_xyz[0]);
	zassert_equal(16, motion_sensors[0].spoof_xyz[1]);
	zassert_equal(-32, motion_sensors[0].spoof_xyz[2]);

	zassert_ok(host_cmd_motion_sense_spoof(0, MOTIONSENSE_SPOOF_MODE_QUERY,
					       0, 0, 0, &response),
		   NULL);
	zassert_true(response.spoof.ret);
}

ZTEST(host_cmd_motion_sense, test_spoof_lock_current)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].raw_xyz[0] = 64;
	motion_sensors[0].raw_xyz[1] = 48;
	motion_sensors[0].raw_xyz[2] = 32;
	zassert_ok(host_cmd_motion_sense_spoof(
			   0, MOTIONSENSE_SPOOF_MODE_LOCK_CURRENT, 0, 0, 0,
			   &response),
		   NULL);
	zassert_equal(MOTIONSENSE_FLAG_IN_SPOOF_MODE,
		      motion_sensors[0].flags & MOTIONSENSE_FLAG_IN_SPOOF_MODE,
		      NULL);
	zassert_equal(64, motion_sensors[0].spoof_xyz[0]);
	zassert_equal(48, motion_sensors[0].spoof_xyz[1]);
	zassert_equal(32, motion_sensors[0].spoof_xyz[2]);

	zassert_ok(host_cmd_motion_sense_spoof(0, MOTIONSENSE_SPOOF_MODE_QUERY,
					       0, 0, 0, &response),
		   NULL);
	zassert_true(response.spoof.ret);
}

ZTEST(host_cmd_motion_sense, test_spoof_invalid_mode)
{
	struct ec_response_motion_sense response;

	zassert_equal(EC_RES_INVALID_PARAM,
		      host_cmd_motion_sense_spoof(0, 0xff, 0, 0, 0, &response),
		      NULL);
}

ZTEST(host_cmd_motion_sense, test_set_kb_wake_lid_angle)
{
	struct ec_response_motion_sense response;
	int16_t expected_lid_angle = 45;
	int rv;

	rv = host_cmd_motion_sense_kb_wake_angle(expected_lid_angle, &response);

	zassert_ok(rv, "Got %d", rv);
	zassert_equal(expected_lid_angle, lid_angle_get_wake_angle());
	zassert_equal(expected_lid_angle, response.kb_wake_angle.ret);
}

ZTEST(host_cmd_motion_sense, test_get_lid_angle)
{
	struct ec_response_motion_sense response;
	int rv;

	rv = host_cmd_motion_sense_lid_angle(&response);

	zassert_ok(rv, "Got %d", rv);
	zassert_equal(motion_lid_get_angle(), response.lid_angle.value);
}

ZTEST(host_cmd_motion_sense, test_tablet_mode_lid_angle)
{
	struct ec_response_motion_sense response;
	int16_t expected_angle = 45;
	int16_t expected_hys = 3;
	int rv;

	rv = host_cmd_motion_sense_tablet_mode_lid_angle(
		expected_angle, expected_hys, &response);

	zassert_ok(rv, "Got %d", rv);
	zassert_equal(expected_angle, response.tablet_mode_threshold.lid_angle);
	zassert_equal(expected_hys, response.tablet_mode_threshold.hys_degree);
}

ZTEST(host_cmd_motion_sense, test_tablet_mode_lid_angle__invalid)
{
	struct ec_response_motion_sense response;

	zassert_ok(!host_cmd_motion_sense_tablet_mode_lid_angle(-100, -100,
								&response));
}
