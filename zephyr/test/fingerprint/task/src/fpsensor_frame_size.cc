/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"

#include <zephyr/fff.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <drivers/fingerprint_sim.h>
#include <fingerprint/v4l2_types.h>
#include <fpsensor/fpsensor_frame_size.h>
#include <memory>
#include <mkbp_event.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, mkbp_send_event, uint8_t);

#define IMAGE_SIZE                                                 \
	MAX_FROM_LIST(LISTIFY(NUM_IMAGE_CAPTURE_TYPES,             \
			      FINGERPRINT_SENSOR_FRAME_SIZE, (, ), \
			      DT_CHOSEN(cros_fp_fingerprint_sensor)))
static uint8_t __unused image_buffer[IMAGE_SIZE];

#define FP_SIMULATOR_IMAGE_FRAME_PARAM_INITIALIZER(idx, node_id)            \
	{                                                                   \
		.frame_size = FINGERPRINT_SENSOR_FRAME_SIZE(idx, node_id),  \
		.pixel_format =                                             \
			FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(idx, node_id), \
		.width = FINGERPRINT_SENSOR_RES_X(idx, node_id),            \
		.height = FINGERPRINT_SENSOR_RES_Y(idx, node_id),           \
		.bpp = FINGERPRINT_SENSOR_RES_BPP(idx, node_id),            \
		.fp_capture_type =                                          \
			FINGERPRINT_SENSOR_CAPTURE_TYPE(idx, node_id),      \
		.reserved = 0,                                              \
	}

static const struct fingerprint_image_frame_params image_frame_params_arr[] = {
	LISTIFY(NUM_IMAGE_CAPTURE_TYPES,
		FP_SIMULATOR_IMAGE_FRAME_PARAM_INITIALIZER, (, ),
		DT_NODELABEL(fpsensor_sim))
};

static std::unique_ptr<FpFrameSizeCache> cache_fixture;

static void fpsensor_frame_size_setup(void *fixture)
{
	ARG_UNUSED(fixture);

	cache_fixture = std::make_unique<FpFrameSizeCache>();
	zassert_not_null(cache_fixture.get(),
			 "Memory allocation failed for cache.");

	cache_fixture->populate_cache(sizeof(image_buffer));
}

ZTEST_SUITE(fpsensor_frame_size, NULL, NULL, fpsensor_frame_size_setup, NULL,
	    NULL);

ZTEST(fpsensor_frame_size, test_cache_initialization_on_size_constraint_failure)
{
	zassert_true(ARRAY_SIZE(image_frame_params_arr) > 0,
		     "image_frame_params_arr must not be empty for this test.");

	uint32_t max_frame_size_bytes =
		image_frame_params_arr[0].frame_size - 1;

	FpFrameSizeCache failing_cache{};
	failing_cache.populate_cache(max_frame_size_bytes);

	for (int i = 0; i < FP_CAPTURE_TYPE_MAX; ++i) {
		enum fp_capture_type type =
			static_cast<enum fp_capture_type>(i);
		uint32_t actual_size = failing_cache.get_frame_size(type);

		zassert_equal(0, actual_size,
			      "Unconfigured Type %d should return zero size.",
			      i);
	}
}

ZTEST(fpsensor_frame_size, test_cache_valid_type_initialization_and_lookup)
{
	FpFrameSizeCache *cache = cache_fixture.get();

	for (const auto &params : image_frame_params_arr) {
		const uint32_t expected_size = params.frame_size;
		const enum fp_capture_type type =
			static_cast<enum fp_capture_type>(
				params.fp_capture_type);

		zassert_true(
			type >= 0 && type < FP_CAPTURE_TYPE_MAX,
			"Configured type %d is out of expected range [0, %d).",
			type, FP_CAPTURE_TYPE_MAX);

		uint32_t actual_size = cache->get_frame_size(type);
		zassert_equal(
			expected_size, actual_size,
			"Mismatch for configured type %d. Expected: %u, Actual: %u",
			static_cast<int>(type), expected_size, actual_size);
	}
}

ZTEST(fpsensor_frame_size, test_cache_invalid_type_lookup)
{
	FpFrameSizeCache *cache = cache_fixture.get();

	std::array<bool, FP_CAPTURE_TYPE_MAX> is_type_present = {};
	for (const auto &params : image_frame_params_arr) {
		is_type_present[static_cast<enum fp_capture_type>(
			params.fp_capture_type)] = true;
	}

	for (int i = 0; i < FP_CAPTURE_TYPE_MAX; ++i) {
		enum fp_capture_type type =
			static_cast<enum fp_capture_type>(i);
		uint32_t actual_size = cache->get_frame_size(type);

		if (is_type_present[i]) {
			zassert_true(
				actual_size > 0,
				"Configured Type %d should return non-zero size.",
				i);
		} else {
			zassert_equal(
				0, actual_size,
				"Unconfigured Type %d should return zero size.",
				i);
		}
	}
}

ZTEST(fpsensor_frame_size, test_cache_boundary_max_index)
{
	FpFrameSizeCache *cache = cache_fixture.get();

	zassert_equal(
		cache->get_frame_size(FP_CAPTURE_TYPE_MAX), 0,
		"Expected zero size for invalid capture type (FP_CAPTURE_TYPE_MAX).");
}

ZTEST(fpsensor_frame_size, test_cache_boundary_negative_index)
{
	FpFrameSizeCache *cache = cache_fixture.get();

	zassert_equal(
		cache->get_frame_size(static_cast<enum fp_capture_type>(-1)), 0,
		"Expected zero size for negative capture type (-1).");
}
