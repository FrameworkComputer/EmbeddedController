/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ft98xx_pal_test_helpers.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <drivers/fingerprint.h>
#include <emul/emul_ft98xx.h>
#include <fingerprint/v4l2_types.h>
#include <fingerprint_ft98xx.h>
#include <fingerprint_ft98xx_private.h>
#include <fpsensor_driver.h>

DEFINE_FFF_GLOBALS;

struct ft98xx_fixture {
	const struct device *dev;
	const struct emul *target;
};

static void *ft98xx_setup(void)
{
	static struct ft98xx_fixture fixture = {
		.dev = DEVICE_DT_GET(DT_NODELABEL(ft98xx)),
		.target = EMUL_DT_GET(DT_NODELABEL(ft98xx)),
	};

	zassert_not_null(fixture.dev);
	zassert_not_null(fixture.target);
	return &fixture;
}

#define FT98XX_IMAGE_FRAME_PARAM_INITIALIZER(idx, node_id)                  \
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

static const struct fingerprint_image_frame_params
	expected_image_frame_params_array[] = { LISTIFY(
		NUM_IMAGE_CAPTURE_TYPES, FT98XX_IMAGE_FRAME_PARAM_INITIALIZER,
		(, ), DT_NODELABEL(ft98xx)) };

ZTEST_SUITE(ft98xx, NULL, ft98xx_setup, NULL, NULL, NULL);

ZTEST_F(ft98xx, test_init_success)
{
	zassert_ok(fingerprint_init(fixture->dev));
}

ZTEST_F(ft98xx, test_deinit_success)
{
	zassert_ok(fingerprint_deinit(fixture->dev));
}

ZTEST_F(ft98xx, test_get_info)
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

	zassert_equal(sensor_info.vendor_id, FOURCC('F', 'T', ' ', ' '));
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

ZTEST_F(ft98xx, test_invalid_mode_not_supported)
{
	zassert_equal(fingerprint_set_mode(fixture->dev, UINT16_MAX), -ENOTSUP);
}

ZTEST_F(ft98xx, test_maintenance)
{
	uint8_t buffer[FP_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_maintenance(fixture->dev, buffer,
					      sizeof(buffer)),
		      0);
}

ZTEST_F(ft98xx, test_finger_status_not_supported)
{
	zassert_equal(fingerprint_finger_status(fixture->dev), -ENOTSUP);
}

ZTEST_F(ft98xx, test_acquire_image_not_supported)
{
	uint8_t buffer[FP_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_acquire_image(fixture->dev, 0, buffer,
						sizeof(buffer)),
		      -ENOTSUP);
}

ZTEST_F(ft98xx, test_acquire_image_small_buffer_size)
{
	uint8_t buffer[FP_SENSOR_IMAGE_SIZE] = { 0 };
	size_t image_buf_size = FP_SENSOR_IMAGE_SIZE - 1;
	enum fingerprint_capture_type capture_type =
		FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT;

	zassert_equal(fingerprint_acquire_image(fixture->dev, capture_type,
						buffer, image_buf_size),
		      -EINVAL);
}

ZTEST_F(ft98xx, test_periphery_spi_write_read_success)
{
	uint8_t tx_buf[1] = { 0xFD };
	uint8_t rx_buf[3] = { 0 };
	uint8_t expected_rx_buf[2] = { 0x49, 0x98 };

	zassert_ok(fingerprint_init(fixture->dev));
	zassert_ok(ft98xx_spi_write_then_read(tx_buf, sizeof(tx_buf), rx_buf,
					      sizeof(rx_buf)));
	zassert_mem_equal(rx_buf, expected_rx_buf, sizeof(expected_rx_buf),
			  "Received data does not match sent data");
}

ZTEST_F(ft98xx, test_periphery_spi_write_read_spi_stopped)
{
	uint8_t tx_buf[1] = { 0xFD };
	uint8_t rx_buf[3] = { 0 };

	zassert_ok(fingerprint_init(fixture->dev));
	ft98xx_stop_spi(fixture->target);
	zassert_equal(ft98xx_spi_write_then_read(tx_buf, sizeof(tx_buf), rx_buf,
						 sizeof(rx_buf)),
		      -EINVAL);
}

ZTEST_F(ft98xx, test_periphery_spi_write_success)
{
	uint8_t tx_buf[1] = { 0xFE };

	zassert_ok(fingerprint_init(fixture->dev));
	zassert_ok(ft98xx_spi_write(tx_buf, sizeof(tx_buf)));
}

ZTEST_F(ft98xx, test_periphery_spi_write_stopped)
{
	uint8_t tx_buf[1] = { 0xFD };

	zassert_ok(fingerprint_init(fixture->dev));
	ft98xx_stop_spi(fixture->target);
	zassert_equal(ft98xx_spi_write(tx_buf, sizeof(tx_buf)), -EINVAL);
}

ZTEST_F(ft98xx, test_sensor_hw_reset)
{
	zassert_ok(fingerprint_init(fixture->dev));
	zassert_ok(ft98xx_sensor_hw_reset());
}

ZTEST_F(ft98xx, test_focal_malloc)
{
	size_t size = sizeof(int);
	void *void_ptr = ft98xx_focal_malloc(size);
	zassert_not_null(void_ptr,
			 "focal_malloc should return a valid pointer");

	int *int_ptr = (int *)void_ptr;
	int num = 9349;
	*int_ptr = num;
	zassert_equal(*int_ptr, num);

	ft98xx_focal_free(void_ptr);
}
