/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "system.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor.h>
#include <fpsensor/fpsensor_state_driver.h>
#include <fpsensor/fpsensor_utils.h>
#include <fpsensor_driver.h>
#include <host_command.h>
#include <mkbp_event.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

static const size_t fp_info_v3_buffer_size =
	sizeof(struct ec_response_fp_info_v3) +
	sizeof(struct fp_image_frame_params_v2) * FP_MAX_CAPTURE_TYPES;
static uint8_t fp_info_v3_buffer[fp_info_v3_buffer_size];
static struct ec_response_fp_info_v3 *test_fp_info_v3_buffer =
	(struct ec_response_fp_info_v3 *)fp_info_v3_buffer;

/* Use the same response buffer size to store FP info data in v3 format. */
static const size_t fp_info_v2_buffer_size = fp_info_v3_buffer_size;
static uint8_t fp_info_v2_buffer[fp_info_v2_buffer_size];
static struct ec_response_fp_info_v2 *test_fp_info_v2_buffer =
	(struct ec_response_fp_info_v2 *)fp_info_v2_buffer;

ZTEST(fp_info, test_v2_v3)
{
	zassert_ok(ec_cmd_fp_info_v2(NULL, test_fp_info_v2_buffer,
				     fp_info_v2_buffer_size));
	zassert_ok(ec_cmd_fp_info_v3(NULL, test_fp_info_v3_buffer,
				     fp_info_v3_buffer_size));

	zassert_equal(test_fp_info_v2_buffer->sensor_info.vendor_id,
		      test_fp_info_v3_buffer->sensor_info.vendor_id);
	zassert_equal(test_fp_info_v2_buffer->sensor_info.product_id,
		      test_fp_info_v3_buffer->sensor_info.product_id);
	zassert_equal(test_fp_info_v2_buffer->sensor_info.model_id,
		      test_fp_info_v3_buffer->sensor_info.model_id);
	zassert_equal(test_fp_info_v2_buffer->sensor_info.version,
		      test_fp_info_v3_buffer->sensor_info.version);
	zassert_equal(test_fp_info_v2_buffer->sensor_info.num_capture_types,
		      test_fp_info_v3_buffer->sensor_info.num_capture_types);
	zassert_equal(test_fp_info_v2_buffer->sensor_info.errors,
		      test_fp_info_v3_buffer->sensor_info.errors);

	zassert_equal(test_fp_info_v2_buffer->template_info.template_size,
		      test_fp_info_v3_buffer->template_info.template_size);
	zassert_equal(test_fp_info_v2_buffer->template_info.template_max,
		      test_fp_info_v3_buffer->template_info.template_max);
	zassert_equal(test_fp_info_v2_buffer->template_info.template_valid,
		      test_fp_info_v3_buffer->template_info.template_valid);
	zassert_equal(test_fp_info_v2_buffer->template_info.template_dirty,
		      test_fp_info_v3_buffer->template_info.template_dirty);
	zassert_equal(test_fp_info_v2_buffer->template_info.template_version,
		      test_fp_info_v3_buffer->template_info.template_version);
	for (int i = 0; i < FP_MAX_CAPTURE_TYPES; i++) {
		zassert_equal(
			test_fp_info_v2_buffer->image_frame_params[i].frame_size,
			test_fp_info_v3_buffer->image_frame_params[i]
				.frame_size);
		zassert_equal(test_fp_info_v2_buffer->image_frame_params[i]
				      .pixel_format,
			      test_fp_info_v3_buffer->image_frame_params[i]
				      .pixel_format);
		zassert_equal(
			test_fp_info_v2_buffer->image_frame_params[i].width,
			test_fp_info_v3_buffer->image_frame_params[i].width);
		zassert_equal(
			test_fp_info_v2_buffer->image_frame_params[i].height,
			test_fp_info_v3_buffer->image_frame_params[i].height);
		zassert_equal(
			test_fp_info_v2_buffer->image_frame_params[i].bpp,
			test_fp_info_v3_buffer->image_frame_params[i].bpp);
		zassert_equal(test_fp_info_v2_buffer->image_frame_params[i]
				      .fp_capture_type,
			      test_fp_info_v3_buffer->image_frame_params[i]
				      .fp_capture_type);
		zassert_equal(
			test_fp_info_v2_buffer->image_frame_params[i].reserved,
			test_fp_info_v3_buffer->image_frame_params[i].reserved);
	}
}
ZTEST_SUITE(fp_info, NULL, NULL, NULL, NULL, NULL);
