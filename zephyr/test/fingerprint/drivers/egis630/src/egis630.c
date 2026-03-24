/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "egis630_pal_test_helpers.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <drivers/fingerprint.h>
#include <emul/emul_egis630.h>
#include <fingerprint/v4l2_types.h>
#include <fingerprint_egis630.h>
#include <fingerprint_egis630_private.h>

DEFINE_FFF_GLOBALS;

extern char printf_buffer[256];
extern LOG_LEVEL g_log_level;
struct egis630_fixture {
	const struct device *dev;
	const struct emul *target;
};

static void *egis630_setup(void)
{
	static struct egis630_fixture fixture = {
		.dev = DEVICE_DT_GET(DT_NODELABEL(egis630)),
		.target = EMUL_DT_GET(DT_NODELABEL(egis630)),
	};

	zassert_not_null(fixture.dev);
	zassert_not_null(fixture.target);
	return &fixture;
}

static void egis630_before(void *data)
{
	memset(printf_buffer, 0, sizeof(printf_buffer));
	g_log_level = LOG_INFO;

	return;
}

/* Maps Egis image capture errors to fingerprint errors. */
int convert_egis_get_image_error_code(egis_api_return_t code);

/* Converts capture types from the ec domain to the egis domain. */
egis_capture_mode_t convert_fp_capture_type_to_egis_capture_type(
	enum fingerprint_capture_type capture_type);

/* Converts Egis sensor initialization error to a generic sensor error code. */
uint16_t convert_egis_sensor_init_error_code(egis_api_return_t code);

#define EGIS630_IMAGE_FRAME_PARAM_INITIALIZER(idx, node_id)                 \
	{                                                                   \
		.frame_size = FINGERPRINT_SENSOR_FRAME_SIZE(idx, node_id),  \
		.image_data_offset_bytes =                                  \
			FINGERPRINT_SENSOR_IMAGE_OFFSET(idx, node_id),      \
		.pixel_format =                                             \
			FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(idx, node_id), \
		.width = FINGERPRINT_SENSOR_RES_X(idx, node_id),            \
		.height = FINGERPRINT_SENSOR_RES_Y(idx, node_id),           \
		.bpp = FINGERPRINT_SENSOR_RES_BPP(idx, node_id),            \
		.fp_capture_type =                                          \
			FINGERPRINT_SENSOR_CAPTURE_TYPE(idx, node_id),      \
		.reserved = 0,                                              \
	}

static const struct fingerprint_image_frame_params
	expected_image_frame_params_array[] = { LISTIFY(
		NUM_IMAGE_CAPTURE_TYPES, EGIS630_IMAGE_FRAME_PARAM_INITIALIZER,
		(, ), DT_NODELABEL(egis630)) };

ZTEST_SUITE(egis630, NULL, egis630_setup, egis630_before, NULL, NULL);

ZTEST_F(egis630, test_init_success)
{
	zassert_ok(fingerprint_init(fixture->dev));
}

ZTEST_F(egis630, test_deinit_success)
{
	zassert_ok(fingerprint_deinit(fixture->dev));
}

ZTEST_F(egis630, test_convert_egis_get_image_error_code)
{
	zassert_equal(
		convert_egis_get_image_error_code(EGIS_API_IMAGE_QUALITY_GOOD),
		FINGERPRINT_SENSOR_SCAN_GOOD);
	zassert_equal(
		convert_egis_get_image_error_code(EGIS_API_IMAGE_QUALITY_BAD),
		FINGERPRINT_SENSOR_SCAN_LOW_IMAGE_QUALITY);
	zassert_equal(
		convert_egis_get_image_error_code(EGIS_API_IMAGE_QUALITY_WATER),
		FINGERPRINT_SENSOR_SCAN_LOW_IMAGE_QUALITY);
	zassert_equal(convert_egis_get_image_error_code(EGIS_API_IMAGE_EMPTY),
		      FINGERPRINT_SENSOR_SCAN_TOO_FAST);
	zassert_equal(convert_egis_get_image_error_code(
			      EGIS_API_IMAGE_QUALITY_PARTIAL),
		      FINGERPRINT_SENSOR_SCAN_LOW_SENSOR_COVERAGE);
	zassert_equal(convert_egis_get_image_error_code(-3), -3);
}

ZTEST_F(egis630, test_convert_egis_sensor_init_error_code)
{
	zassert_equal(
		convert_egis_sensor_init_error_code(EGIS_API_ERROR_IO_SPI),
		FINGERPRINT_ERROR_SPI_COMM);
	zassert_equal(convert_egis_sensor_init_error_code(
			      EGIS_API_ERROR_DEVICE_NOT_FOUND),
		      FINGERPRINT_ERROR_BAD_HWID);
	zassert_equal(convert_egis_sensor_init_error_code(EGIS_API_OK), 0);
	zassert_equal(convert_egis_sensor_init_error_code(EGIS_API_ERROR),
		      FINGERPRINT_ERROR_INIT_FAIL);
}

ZTEST_F(egis630, test_get_info)
{
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES];
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES;

	zassert_ok(fingerprint_init(fixture->dev));
	zassert_ok(fingerprint_get_info(fixture->dev, &sensor_info,
					image_frame_params_array, &num_params));
	zassert_equal(
		num_params, NUM_IMAGE_CAPTURE_TYPES,
		"fingerprint_get_info returned an unexpected number of params");

	zassert_equal(sensor_info.vendor_id, FOURCC('E', 'G', 'I', 'S'));
	zassert_equal(sensor_info.product_id, 9);
	zassert_equal(sensor_info.version, 1);
	zassert_equal(sensor_info.errors,
		      FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN);

	for (int i = 0; i < NUM_IMAGE_CAPTURE_TYPES; ++i) {
		zassert_equal(
			memcmp(&image_frame_params_array[i],
			       &expected_image_frame_params_array[i],
			       sizeof(struct fingerprint_image_frame_params)),
			0, "Struct comparison failed at index %d", i);
	}
}

ZTEST_F(egis630, test_enter_idle)
{
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_IDLE));
}

ZTEST_F(egis630, test_invalid_mode_not_supported)
{
	zassert_equal(fingerprint_set_mode(fixture->dev, UINT16_MAX), -ENOTSUP);
}

ZTEST_F(egis630, test_maintenance_not_supported)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_maintenance(fixture->dev, buffer,
					      sizeof(buffer)),
		      -ENOTSUP);
}

ZTEST_F(egis630, test_maintenance_image_small_buffer_size)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE - 1];

	zassert_equal(fingerprint_maintenance(fixture->dev, buffer,
					      sizeof(buffer)),
		      -EINVAL);
}

ZTEST_F(egis630, test_finger_status_not_supported)
{
	zassert_equal(fingerprint_finger_status(fixture->dev), -ENOTSUP);
}

ZTEST_F(egis630, test_convert_fp_capture_type_to_egis_capture_type)
{
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT),
		      EGIS_CAPTURE_IMAGE_COLLECTION);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE),
		      EGIS_CAPTURE_NORMAL_FORMAT);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_PATTERN0),
		      EGIS_CAPTURE_BLACK_PXL_TEST);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_PATTERN1),
		      EGIS_CAPTURE_WHITE_PXL_TEST);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_QUALITY_TEST),
		      EGIS_CAPTURE_RV_INT_TEST);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_DEFECT_PXL_TEST),
		      EGIS_CAPTURE_DEFECT_PXL_TEST);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_ABNORMAL_TEST),
		      EGIS_CAPTURE_ABNORMAL_TEST);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_NOISE_TEST),
		      EGIS_CAPTURE_NOISE_TEST);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_RESET_TEST),
		      EGIS_CAPTURE_TYPE_INVALID);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_MAX),
		      EGIS_CAPTURE_TYPE_INVALID);
}

ZTEST_F(egis630, test_acquire_image_small_buffer_size)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE] = { 0 };
	size_t image_buf_size = CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE - 1;
	enum fingerprint_capture_type capture_type =
		FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT;

	zassert_equal(fingerprint_acquire_image(fixture->dev, capture_type,
						buffer, image_buf_size),
		      -EINVAL);
}

ZTEST_F(egis630, test_acquire_image_not_supported)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];
	enum fingerprint_capture_type capture_type =
		FINGERPRINT_CAPTURE_TYPE_RESET_TEST;

	zassert_equal(fingerprint_acquire_image(fixture->dev, capture_type,
						buffer, sizeof(buffer)),
		      -EINVAL);
}

ZTEST_F(egis630, test_acquire_image_wrong_capture_type)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE] = { 0 };
	enum fingerprint_capture_type capture_type =
		FINGERPRINT_CAPTURE_TYPE_MAX;

	zassert_equal(fingerprint_acquire_image(fixture->dev, capture_type,
						buffer, sizeof(buffer)),
		      -EINVAL);
}

ZTEST_F(egis630, test_plat_get_time)
{
	uint64_t time_msecs = egis630_plat_get_time();
	uint64_t ecpected_time_mecs = k_uptime_get();
	zassert_equal(time_msecs, ecpected_time_mecs);
}

ZTEST_F(egis630, test_plat_wait_time)
{
	uint64_t msecs = 30;
	uint64_t tick_msecs = 1000 / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
	uint64_t t1, t2;

	t1 = k_uptime_get();
	egis630_plat_wait_time(msecs);
	t2 = k_uptime_get();

	zassert_within(t2 - t1, msecs, tick_msecs);
}

ZTEST_F(egis630, test_plat_sleep_time)
{
	uint64_t msecs = 30;
	uint64_t tick_msecs = 1000 / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
	uint64_t t1, t2;

	t1 = k_uptime_get();
	egis630_plat_sleep_time(msecs);
	t2 = k_uptime_get();

	zassert_within(t2 - t1, msecs, tick_msecs);
}

ZTEST_F(egis630, test_plat_get_diff_time)
{
	uint64_t begin_time_msec;
	uint64_t time_delta_msecs;
	uint64_t t1;

	begin_time_msec = k_uptime_get();
	k_msleep(30);
	t1 = k_uptime_get();
	time_delta_msecs = egis630_plat_get_diff_time(begin_time_msec);

	zassert_equal(time_delta_msecs, t1 - begin_time_msec);
}

ZTEST_F(egis630, test_sys_alloc_normal)
{
	size_t size = sizeof(int);
	void *void_ptr = sys_alloc(1, size);
	zassert_not_null(void_ptr, "sys_alloc should return a valid pointer");

	int *int_ptr = (int *)void_ptr;
	int num = 4546;
	*int_ptr = num;
	zassert_equal(*int_ptr, num);

	sys_free(void_ptr);
}

ZTEST_F(egis630, test_periphery_spi_write_read_success)
{
	uint8_t tx_buf[1] = { 0xFD };
	uint8_t rx_buf[3] = { 0 };
	uint8_t expeceted_rx_buf[3] = { 0x1, 0x1E, 0x6 };

	zassert_ok(fingerprint_init(fixture->dev));
	zassert_ok(egis630_periphery_spi_write_read(tx_buf, sizeof(tx_buf),
						    rx_buf, sizeof(rx_buf)));
	zassert_mem_equal(rx_buf, expeceted_rx_buf, sizeof(expeceted_rx_buf),
			  "Received data does not match sent data");
}

ZTEST_F(egis630, test_periphery_spi_write_read_spi_stopped)
{
	uint8_t tx_buf[1] = { 0xFD };
	uint8_t rx_buf[3] = { 0 };

	zassert_ok(fingerprint_init(fixture->dev));
	egis630_stop_spi(fixture->target);
	zassert_equal(egis630_periphery_spi_write_read(tx_buf, sizeof(tx_buf),
						       rx_buf, sizeof(rx_buf)),
		      -EIO);
}

ZTEST_F(egis630, test_output_log_success_info)
{
	char expected_printf_buffer[sizeof(printf_buffer)];

	memset(expected_printf_buffer, 0, sizeof(expected_printf_buffer));
	snprintf(expected_printf_buffer, sizeof(expected_printf_buffer),
		 "<func:10> info 5.43");

	output_log(LOG_INFO, "tag", "file", "func", 10, "info %.2f", 5.43);

	zassert_mem_equal(printf_buffer, expected_printf_buffer,
			  sizeof(printf_buffer),
			  "LOG message not match expected message");
}

ZTEST_F(egis630, test_output_log_success_warn)
{
	char expected_printf_buffer[sizeof(printf_buffer)];

	memset(expected_printf_buffer, 0, sizeof(expected_printf_buffer));
	snprintf(expected_printf_buffer, sizeof(expected_printf_buffer),
		 "<func:10> warn 7.6");

	output_log(LOG_WARN, "tag", "file", "func", 10, "warn %.1f", 7.6);
	zassert_mem_equal(printf_buffer, expected_printf_buffer,
			  sizeof(printf_buffer),
			  "LOG message not match expected message");
}

ZTEST_F(egis630, test_output_log_success_error)
{
	char expected_printf_buffer[sizeof(printf_buffer)];

	memset(expected_printf_buffer, 0, sizeof(expected_printf_buffer));
	snprintf(expected_printf_buffer, sizeof(expected_printf_buffer),
		 "<func:10> error 2.1");

	output_log(LOG_ERROR, "tag", "file", "func", 10, "error %.1f", 2.1);
	zassert_mem_equal(printf_buffer, expected_printf_buffer,
			  sizeof(printf_buffer),
			  "LOG message not match expected message");
}

ZTEST_F(egis630, test_output_log_success_assert)
{
	char expected_printf_buffer[sizeof(printf_buffer)];

	memset(expected_printf_buffer, 0, sizeof(expected_printf_buffer));
	snprintf(expected_printf_buffer, sizeof(expected_printf_buffer),
		 "<func:10> assert 3.1");

	output_log(LOG_WARN, "tag", "file", "func", 10, "assert %.1f", 3.1);
	zassert_mem_equal(printf_buffer, expected_printf_buffer,
			  sizeof(printf_buffer),
			  "LOG message not match expected message");
}

ZTEST_F(egis630, test_output_log_format_NULL)
{
	output_log(LOG_INFO, "tag", "file", "func", 10, NULL);
	zassert_equal(printf_buffer[0], '\0');
}

ZTEST_F(egis630, test_output_log_format_no_logging_info_greater_than_verbose)
{
	output_log(LOG_VERBOSE, "tag", "file", "func", 10, "info %.1f", 3.1);
	zassert_equal(printf_buffer[0], '\0');
}

ZTEST_F(egis630, test_output_log_format_no_logging_info_greater_than_debug)
{
	output_log(LOG_DEBUG, "tag", "file", "func", 10, "info %.1f", 2.1f);
	zassert_equal(printf_buffer[0], '\0');
}

ZTEST_F(egis630, test_set_debug_level_success_verbose)
{
	egis630_set_debug_level(LOG_VERBOSE);
	zassert_equal(g_log_level, LOG_VERBOSE);

	char expected_printf_buffer[sizeof(printf_buffer)];

	memset(expected_printf_buffer, 0, sizeof(expected_printf_buffer));
	snprintf(expected_printf_buffer, sizeof(expected_printf_buffer),
		 "<:0> set_debug_level 2");
	zassert_mem_equal(printf_buffer, expected_printf_buffer,
			  sizeof(printf_buffer),
			  "LOG message not match expected message");
}

ZTEST_F(egis630, test_set_debug_level_success_debug)
{
	egis630_set_debug_level(LOG_DEBUG);
	zassert_equal(g_log_level, LOG_DEBUG);

	char expected_printf_buffer[sizeof(printf_buffer)];

	memset(expected_printf_buffer, 0, sizeof(expected_printf_buffer));
	snprintf(expected_printf_buffer, sizeof(expected_printf_buffer),
		 "<:0> set_debug_level 3");
	zassert_mem_equal(printf_buffer, expected_printf_buffer,
			  sizeof(printf_buffer),
			  "LOG message not match expected message");
}

ZTEST_F(egis630, test_set_debug_level_success_info)
{
	egis630_set_debug_level(LOG_INFO);
	zassert_equal(g_log_level, LOG_INFO);

	char expected_printf_buffer[sizeof(printf_buffer)];

	memset(expected_printf_buffer, 0, sizeof(expected_printf_buffer));
	snprintf(expected_printf_buffer, sizeof(expected_printf_buffer),
		 "<:0> set_debug_level 4");
	zassert_mem_equal(printf_buffer, expected_printf_buffer,
			  sizeof(printf_buffer),
			  "LOG message not match expected message");
}

ZTEST_F(egis630, test_set_debug_level_success_warn)
{
	egis630_set_debug_level(LOG_WARN);
	zassert_equal(g_log_level, LOG_WARN);

	char expected_printf_buffer[sizeof(printf_buffer)];

	memset(expected_printf_buffer, 0, sizeof(expected_printf_buffer));
	snprintf(expected_printf_buffer, sizeof(expected_printf_buffer),
		 "<:0> set_debug_level 5");
	zassert_mem_equal(printf_buffer, expected_printf_buffer,
			  sizeof(printf_buffer),
			  "LOG message not match expected message");
}

ZTEST_F(egis630, test_set_debug_level_success_error)
{
	egis630_set_debug_level(LOG_ERROR);
	zassert_equal(g_log_level, LOG_ERROR);

	char expected_printf_buffer[sizeof(printf_buffer)];

	memset(expected_printf_buffer, 0, sizeof(expected_printf_buffer));
	snprintf(expected_printf_buffer, sizeof(expected_printf_buffer),
		 "<:0> set_debug_level 6");
	zassert_mem_equal(printf_buffer, expected_printf_buffer,
			  sizeof(printf_buffer),
			  "LOG message not match expected message");
}

ZTEST_F(egis630, test_set_debug_level_success_assert)
{
	egis630_set_debug_level(LOG_ASSERT);
	zassert_equal(g_log_level, LOG_ASSERT);

	zassert_equal(printf_buffer[0], '\0');
}

ZTEST_F(egis630, test_PLAT_FREE_success)
{
	int num = 1234;
	int *ptr = (int *)sys_alloc(1, sizeof(int));
	*ptr = num;

	zassert_not_null(ptr);
	zassert_equal(*ptr, num);

	PLAT_FREE((void **)&ptr);
	zassert_is_null(ptr);
}

ZTEST_F(egis630, test_egis630_rbs_check_if_null_error)
{
	int error_code = 123;
	char expected_printf_buffer[sizeof(printf_buffer)];

	memset(expected_printf_buffer, 0, sizeof(expected_printf_buffer));
	snprintf(expected_printf_buffer, sizeof(expected_printf_buffer),
		 "<z_impl_egis630_rbs_check_if_null:53> "
		 "z_impl_egis630_rbs_check_if_null, ptr is NULL");

	int result = egis630_rbs_check_if_null(NULL, error_code);

	zassert_equal(result, error_code);
	zassert_mem_equal(printf_buffer, expected_printf_buffer,
			  sizeof(printf_buffer),
			  "LOG message not match expected message");
}

ZTEST_F(egis630, test_egis630_rbs_check_if_null_no_error)
{
	int error_code = 123;
	int buffer[8] = { 0 };

	int result = egis630_rbs_check_if_null(buffer, error_code);

	zassert_equal(result, 0);
	zassert_equal(printf_buffer[0], '\0');
}

ZTEST_F(egis630, test_egislog_e)
{
	egislog_e("The output is %d and %.1f", 6, 2.3);
	char expected_printf_buffer[sizeof(printf_buffer)];
	memset(expected_printf_buffer, 0, sizeof(expected_printf_buffer));
	snprintf(expected_printf_buffer, sizeof(expected_printf_buffer),
		 "<egis630_test_egislog_e:%d> The output is 6 and 2.3",
		 __LINE__ - 5);
	zassert_mem_equal(printf_buffer, expected_printf_buffer,
			  sizeof(printf_buffer),
			  "LOG message not match expected message");
}

ZTEST_F(egis630, test_egislog_d)
{
	egislog_d("The output is %d and %.1f", 6, 2.3);
	zassert_equal(printf_buffer[0], '\0');
}

ZTEST_F(egis630, test_egislog_i)
{
	egislog_i("The output is %d and %.1f", 6, 2.3);
	char expected_printf_buffer[sizeof(printf_buffer)];
	memset(expected_printf_buffer, 0, sizeof(expected_printf_buffer));
	snprintf(expected_printf_buffer, sizeof(expected_printf_buffer),
		 "<egis630_test_egislog_i:%d> The output is 6 and 2.3",
		 __LINE__ - 5);
	zassert_mem_equal(printf_buffer, expected_printf_buffer,
			  sizeof(printf_buffer),
			  "LOG message not match expected message");
}

ZTEST_F(egis630, test_egislog_v)
{
	egislog_v("The output is %d and %.1f", 6, 2.3);
	zassert_equal(printf_buffer[0], '\0');
}
