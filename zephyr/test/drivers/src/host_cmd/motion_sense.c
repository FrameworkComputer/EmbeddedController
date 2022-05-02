/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fff.h>
#include <ztest.h>

#include "atomic.h"
#include "driver/accel_bma2x2.h"
#include "motion_sense.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

FAKE_VALUE_FUNC(int, mock_set_range, struct motion_sensor_t *, int, int);
FAKE_VALUE_FUNC(int, mock_set_offset, const struct motion_sensor_t *,
		const int16_t *, int16_t);
FAKE_VALUE_FUNC(int, mock_get_offset, const struct motion_sensor_t *, int16_t *,
		int16_t *);
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
	(sizeof(struct ec_response_motion_sense) + \
	 n * sizeof(uint16_t))

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

	atomic_clear(&motion_sensors[0].flush_pending);
	motion_sensors[0].config[SENSOR_CONFIG_AP].odr = 0;
	motion_sensors[0].config[SENSOR_CONFIG_AP].ec_rate = 1000 * MSEC;
}

static void host_cmd_motion_sense_after(void *fixture)
{
	struct host_cmd_motion_sense_fixture *this = fixture;

	motion_sensors[0].drv = this->sensor_0_drv;
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
	host_cmd_motion_sense_dump(ALL_MOTION_SENSORS, result);

	zassert_equal(result->dump.module_flags, MOTIONSENSE_MODULE_FLAG_ACTIVE,
		      NULL);
	zassert_equal(result->dump.sensor_count, ALL_MOTION_SENSORS, NULL);

	/*
	 * Test the values returned in the dump. Normally we shouldn't be doing
	 * tests in a loop, but since the number of sensors (as well as the
	 * order) is adjustable by devicetree, it would be too difficult to hard
	 * code here.
	 */
	for (int i = 0; i < ALL_MOTION_SENSORS; ++i) {
		zassert_equal(result->dump.sensor[i].flags,
			      MOTIONSENSE_SENSOR_FLAG_PRESENT, NULL);
		zassert_equal(result->dump.sensor[i].data[0], i, NULL);
		zassert_equal(result->dump.sensor[i].data[1], i + 1, NULL);
		zassert_equal(result->dump.sensor[i].data[2], i + 2, NULL);
	}
}

ZTEST_USER(host_cmd_motion_sense, test_dump__large_max_sensor_count)
{
	uint8_t response_buffer[RESPONSE_MOTION_SENSE_BUFFER_SIZE(
		ALL_MOTION_SENSORS)];
	struct ec_response_motion_sense *result =
		(struct ec_response_motion_sense *)response_buffer;

	host_cmd_motion_sense_dump(ALL_MOTION_SENSORS + 1, result);

	zassert_equal(result->dump.sensor_count, ALL_MOTION_SENSORS, NULL);
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

	zassert_ok(host_cmd_motion_sense_data(0, &response), NULL);
	zassert_equal(response.data.flags, 0, NULL);
	zassert_equal(response.data.data[0], 1, NULL);
	zassert_equal(response.data.data[1], 2, NULL);
	zassert_equal(response.data.data[2], 3, NULL);
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
	zassert_equal(response.info.type, motion_sensors[0].type, NULL);
	zassert_equal(response.info.location, motion_sensors[0].location, NULL);
	zassert_equal(response.info.chip, motion_sensors[0].chip, NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_get_info_v3)
{
	struct ec_response_motion_sense response;

	zassert_ok(host_cmd_motion_sense_info(/*cmd_version=*/3,
					      /*sensor_num=*/0, &response),
		   NULL);
	zassert_equal(response.info.type, motion_sensors[0].type, NULL);
	zassert_equal(response.info.location, motion_sensors[0].location, NULL);
	zassert_equal(response.info.chip, motion_sensors[0].chip, NULL);
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
	zassert_equal(response.info.type, motion_sensors[0].type, NULL);
	zassert_equal(response.info.location, motion_sensors[0].location, NULL);
	zassert_equal(response.info.chip, motion_sensors[0].chip, NULL);
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

	zassert_ok(host_cmd_motion_sense_ec_rate(
			   /*sensor_num=*/0,
			   /*data_rate_ms=*/EC_MOTION_SENSE_NO_VALUE,
			   &response),
		   NULL);
	zassert_equal(response.ec_rate.ret, 1000, NULL);
}

ZTEST_USER(host_cmd_motion_sense, test_set_ec_rate)
{
	struct ec_response_motion_sense response;

	zassert_ok(host_cmd_motion_sense_ec_rate(
			   /*sensor_num=*/0, /*data_rate_ms=*/2000, &response),
		   NULL);
	/* The command should return the previous rate */
	zassert_equal(response.ec_rate.ret, 1000, "Expected 1000, but got %d",
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

	zassume_ok(motion_sensors[0].drv->set_data_rate(&motion_sensors[0],
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

	zassume_ok(motion_sensors[0].drv->set_data_rate(&motion_sensors[0], 0,
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
	motion_sensors[0].drv = &this->mock_drv;

	zassert_equal(EC_RES_INVALID_PARAM,
		      host_cmd_motion_sense_range(/*sensor_num=*/0, /*range=*/4,
						  /*round_up=*/false,
						  &response),
		      NULL);
	zassert_equal(1, mock_set_range_fake.call_count, NULL);
}

ZTEST_USER_F(host_cmd_motion_sense, test_set_range)
{
	struct ec_response_motion_sense response;

	mock_set_range_fake.return_val = 0;
	motion_sensors[0].drv = &this->mock_drv;

	zassert_ok(host_cmd_motion_sense_range(/*sensor_num=*/0, /*range=*/4,
					       /*round_up=*/false, &response),
		   NULL);
	zassert_equal(1, mock_set_range_fake.call_count, NULL);
	zassert_equal(4, mock_set_range_fake.arg1_history[0], NULL);
	zassert_equal(0, mock_set_range_fake.arg2_history[0], NULL);
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

	motion_sensors[0].drv = &this->mock_drv;
	mock_set_offset_fake.return_val = EC_RES_ERROR;

	zassert_equal(EC_RES_ERROR,
		      host_cmd_motion_sense_offset(
			      /*sensor_num=*/0,
			      /*flags=*/MOTION_SENSE_SET_OFFSET,
			      /*temperature=*/0, /*offset_x=*/0,
			      /*offset_y=*/0, /*offset_z=*/0, &response),
		      NULL);
	zassert_equal(1, mock_set_offset_fake.call_count, NULL);
}

ZTEST_USER_F(host_cmd_motion_sense, test_offset_fail_to_get)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &this->mock_drv;
	mock_set_offset_fake.return_val = EC_RES_SUCCESS;
	mock_get_offset_fake.return_val = EC_RES_ERROR;

	zassert_equal(EC_RES_ERROR,
		      host_cmd_motion_sense_offset(
			      /*sensor_num=*/0,
			      /*flags=*/MOTION_SENSE_SET_OFFSET,
			      /*temperature=*/0, /*offset_x=*/0,
			      /*offset_y=*/0, /*offset_z=*/0, &response),
		      NULL);
	zassert_equal(1, mock_set_offset_fake.call_count, NULL);
	zassert_equal(1, mock_get_offset_fake.call_count, NULL);
	zassert_equal((int16_t *)&response.sensor_offset.offset,
		      mock_get_offset_fake.arg1_history[0], NULL);
}

ZTEST_USER_F(host_cmd_motion_sense, test_get_offset)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &this->mock_drv;
	mock_get_offset_fake.return_val = EC_RES_SUCCESS;
	mock_set_offset_fake.return_val = EC_RES_SUCCESS;

	zassert_ok(host_cmd_motion_sense_offset(
			   /*sensor_num=*/0,
			   /*flags=*/MOTION_SENSE_SET_OFFSET,
			   /*temperature=*/1, /*offset_x=*/2,
			   /*offset_y=*/3, /*offset_z=*/4, &response),
		   NULL);
	zassert_equal(1, mock_set_offset_fake.call_count, NULL);
	zassert_equal(1, mock_get_offset_fake.call_count, NULL);
	zassert_equal((int16_t *)&response.sensor_offset.offset,
		      mock_get_offset_fake.arg1_history[0], NULL);
	zassert_equal(1, mock_set_offset_fake.arg2_history[0], NULL);
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

	motion_sensors[0].drv = &this->mock_drv;
	mock_get_scale_fake.return_val = 1;

	zassert_equal(1,
		      host_cmd_motion_sense_scale(
			      /*sensor_num=*/0,
			      /*flags=*/0,
			      /*temperature=*/1, /*scale_x=*/2,
			      /*scale_y=*/3, /*scale_z=*/4, &response),
		      NULL);
	zassert_equal(1, mock_get_scale_fake.call_count, NULL);
}

ZTEST_USER_F(host_cmd_motion_sense, test_set_scale_fail)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &this->mock_drv;
	mock_set_scale_fake.return_val = 1;

	zassert_equal(1,
		      host_cmd_motion_sense_scale(
			      /*sensor_num=*/0,
			      /*flags=*/MOTION_SENSE_SET_OFFSET,
			      /*temperature=*/1, /*scale_x=*/2,
			      /*scale_y=*/3, /*scale_z=*/4, &response),
		      NULL);
	zassert_equal(1, mock_set_scale_fake.call_count, NULL);
}

ZTEST_USER_F(host_cmd_motion_sense, test_set_get_scale)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &this->mock_drv;
	mock_set_scale_fake.return_val = 0;
	mock_get_scale_fake.return_val = 0;

	zassert_ok(host_cmd_motion_sense_scale(
			   /*sensor_num=*/0,
			   /*flags=*/MOTION_SENSE_SET_OFFSET,
			   /*temperature=*/1, /*scale_x=*/2,
			   /*scale_y=*/3, /*scale_z=*/4, &response),
		   NULL);
	zassert_equal(1, mock_set_scale_fake.call_count, NULL);
	zassert_equal(1, mock_get_scale_fake.call_count, NULL);
	zassert_equal(1, mock_set_scale_fake.arg2_history[0], NULL);
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

	motion_sensors[0].drv = &this->mock_drv;
	mock_perform_calib_fake.return_val = 1;

	zassert_equal(1,
		      host_cmd_motion_sense_calib(/*sensor_num=*/0,
						  /*enable=*/false, &response),
		      NULL);
	zassert_equal(1, mock_perform_calib_fake.call_count, NULL);
	zassert_false(mock_perform_calib_fake.arg1_history[0], NULL);
}

ZTEST_USER_F(host_cmd_motion_sense, test_calib_success__fail_get_offset)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &this->mock_drv;
	mock_perform_calib_fake.return_val = 0;
	mock_get_offset_fake.return_val = 1;

	zassert_equal(1,
		      host_cmd_motion_sense_calib(/*sensor_num=*/0,
						  /*enable=*/false, &response),
		      NULL);
	zassert_equal(1, mock_perform_calib_fake.call_count, NULL);
	zassert_equal(1, mock_get_offset_fake.call_count, NULL);
	zassert_false(mock_perform_calib_fake.arg1_history[0], NULL);
}

ZTEST_USER_F(host_cmd_motion_sense, test_calib)
{
	struct ec_response_motion_sense response;

	motion_sensors[0].drv = &this->mock_drv;
	mock_perform_calib_fake.return_val = 0;
	mock_get_offset_fake.return_val = 0;

	zassert_ok(host_cmd_motion_sense_calib(/*sensor_num=*/0,
					       /*enable=*/true, &response),
		   NULL);
	zassert_equal(1, mock_perform_calib_fake.call_count, NULL);
	zassert_equal(1, mock_get_offset_fake.call_count, NULL);
	zassert_true(mock_perform_calib_fake.arg1_history[0], NULL);
}

ZTEST(host_cmd_motion_sense, test_fifo_flush__invalid_sensor_num)
{
	int rv;
	struct ec_response_motion_sense response;

	rv = host_cmd_motion_sense_fifo_flush(/*sensor_num=*/0xff, &response);
	zassert_equal(rv, EC_RES_INVALID_PARAM, NULL);
}

ZTEST(host_cmd_motion_sense, test_fifo_flush)
{
	uint8_t response_buffer[RESPONSE_SENSOR_FIFO_SIZE(ALL_MOTION_SENSORS)];
	struct ec_response_motion_sense *response =
		(struct ec_response_motion_sense *)response_buffer;

	motion_sensors[0].lost = 5;
	zassert_ok(host_cmd_motion_sense_fifo_flush(/*sensor_num=*/0,
						    response),
		   NULL);
	zassert_equal(1, motion_sensors[0].flush_pending, NULL);
	zassert_equal(5, response->fifo_info.lost[0], NULL);
	zassert_equal(0, motion_sensors[0].lost, NULL);
}
