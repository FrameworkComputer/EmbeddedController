/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpc1145_pal_test_helpers.h"

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
#include <emul/emul_fpc1145.h>
#include <fingerprint/v4l2_types.h>
#include <fingerprint_fpc1145_private.h>

DEFINE_FFF_GLOBALS;

struct fpc1145_fixture {
	const struct device *dev;
	const struct emul *target;
};

static void *fpc1145_setup(void)
{
	static struct fpc1145_fixture fixture = {
		.dev = DEVICE_DT_GET(DT_NODELABEL(fpc1145)),
		.target = EMUL_DT_GET(DT_NODELABEL(fpc1145)),
	};

	zassert_not_null(fixture.dev);
	zassert_not_null(fixture.target);
	return &fixture;
}

/* Converts capture type modes from the ec domain to the fpc domain. */
int convert_fp_capture_mode_to_fpc_get_image_type(int mode);

ZTEST_SUITE(fpc1145, NULL, fpc1145_setup, NULL, NULL, NULL);

ZTEST_F(fpc1145, test_init_success)
{
	zassert_ok(fingerprint_init(fixture->dev));
	/*
	 * TODO(b/117620462): verify that sleep mode is WAI (no increased
	 * latency, expected power consumption).
	 */
	zassert_equal(fpc1145_get_low_power_mode(fixture->target), 0);
}

ZTEST_F(fpc1145, test_init_failure_bad_hwid)
{
	struct fingerprint_info info;

	fpc1145_set_hwid(fixture->target, 0x0);
	zassert_equal(fingerprint_init(fixture->dev), -EINVAL);
	zassert_ok(fingerprint_get_info(fixture->dev, &info));
	zassert_equal(info.errors, (FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN |
				    FINGERPRINT_ERROR_BAD_HWID |
				    FINGERPRINT_ERROR_INIT_FAIL));
}

ZTEST_F(fpc1145, test_init_failure_no_irq)
{
	struct fingerprint_info info;

	fpc1145_stop_irq(fixture->target);
	zassert_equal(fingerprint_init(fixture->dev), -EINVAL);
	zassert_ok(fingerprint_get_info(fixture->dev, &info));
	zassert_equal(info.errors,
		      (FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN |
		       FINGERPRINT_ERROR_NO_IRQ | FINGERPRINT_ERROR_INIT_FAIL));
}

ZTEST_F(fpc1145, test_init_failure_spi)
{
	struct fingerprint_info info;

	fpc1145_stop_spi(fixture->target);
	zassert_equal(fingerprint_init(fixture->dev), -EINVAL);
	zassert_not_ok(fingerprint_get_info(fixture->dev, &info));
	fpc1145_start_spi(fixture->target);
	zassert_ok(fingerprint_get_info(fixture->dev, &info));
	zassert_equal(info.errors, (FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN |
				    FINGERPRINT_ERROR_SPI_COMM |
				    FINGERPRINT_ERROR_INIT_FAIL));
}

ZTEST_F(fpc1145, test_deinit_success)
{
	zassert_ok(fingerprint_deinit(fixture->dev));
}

ZTEST_F(fpc1145, test_get_info)
{
	struct fingerprint_info info;

	/* We need to initialize driver first to initialize 'error' field */
	zassert_ok(fingerprint_init(fixture->dev));
	zassert_ok(fingerprint_get_info(fixture->dev, &info));

	zassert_equal(info.vendor_id, FOURCC('F', 'P', 'C', ' '));
	zassert_equal(info.product_id, 9);
	/*
	 * Last 4 bits of hardware id is a year of sensor production,
	 * could differ between sensors.
	 */
	zassert_equal(info.model_id >> 4, 0x140);
	zassert_equal(info.version, 1);
	zassert_equal(info.frame_size, CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE);
	zassert_equal(info.pixel_format, FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(
						 DT_NODELABEL(fpc1145)));
	zassert_equal(info.width,
		      FINGERPRINT_SENSOR_RES_X(DT_NODELABEL(fpc1145)));
	zassert_equal(info.height,
		      FINGERPRINT_SENSOR_RES_Y(DT_NODELABEL(fpc1145)));
	zassert_equal(info.bpp,
		      FINGERPRINT_SENSOR_RES_BPP(DT_NODELABEL(fpc1145)));
	zassert_equal(info.errors, FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN);
}

ZTEST_F(fpc1145, test_enter_low_power_mode)
{
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_LOW_POWER));
	/*
	 * TODO(b/117620462): verify that sleep mode is WAI (no increased
	 * latency, expected power consumption).
	 */
	zassert_equal(fpc1145_get_low_power_mode(fixture->target), 0);
}

ZTEST_F(fpc1145, test_enter_idle)
{
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_IDLE));
}

ZTEST_F(fpc1145, test_invalid_mode_not_supported)
{
	zassert_equal(fingerprint_set_mode(fixture->dev, UINT16_MAX), -ENOTSUP);
}

FAKE_VOID_FUNC(test_interrupt_handler, const struct device *);

ZTEST_F(fpc1145, test_interrupt)
{
	const struct gpio_dt_spec spec =
		GPIO_DT_SPEC_GET(DT_NODELABEL(fpc1145), irq_gpios);

	RESET_FAKE(test_interrupt_handler);
	zassert_ok(fingerprint_config(fixture->dev, test_interrupt_handler));

	/* Enable interrupt (they are disabled by default). */
	zassert_ok(gpio_pin_interrupt_configure_dt(&spec,
						   GPIO_INT_EDGE_TO_ACTIVE));

	/*
	 * Toggle the GPIO twice. We expect that the driver will disable
	 * interrupt in interrupt handler, so handler should be called once.
	 */
	for (int i = 0; i < 2; i++) {
		gpio_emul_input_set(spec.port, spec.pin, 1);
		k_msleep(5);
		gpio_emul_input_set(spec.port, spec.pin, 0);
		k_msleep(5);
	}

	/* Verify the handler was called once. */
	zassert_equal(test_interrupt_handler_fake.call_count, 1);
}

ZTEST_F(fpc1145, test_maintenance_not_supported)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_maintenance(fixture->dev, buffer,
					      sizeof(buffer)),
		      -ENOTSUP);
}

ZTEST_F(fpc1145, test_finger_status_not_supported)
{
	zassert_equal(fingerprint_finger_status(fixture->dev), -ENOTSUP);
}

ZTEST_F(fpc1145, test_acquire_image_not_supported)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_acquire_image(fixture->dev, 0, buffer,
						sizeof(buffer)),
		      -ENOTSUP);
}

ZTEST_F(fpc1145, test_sensor_mode_detect_not_supported)
{
	zassert_equal(fingerprint_set_mode(fixture->dev,
					   FINGERPRINT_SENSOR_MODE_DETECT),
		      -ENOTSUP);
}

ZTEST_F(fpc1145, test_pal_spi_writeread)
{
	/*
	 * Prepare buffer with command and place for response.
	 * Sensor should return hardware id.
	 */
	uint8_t hwid_cmd_buf[] = { 0xFC, 0x00, 0x00 };
	uint16_t hwid;

	zassert_ok(fpc1145_pal_spi_writeread(NULL, hwid_cmd_buf, hwid_cmd_buf,
					     sizeof(hwid_cmd_buf)));
	/*
	 * Second and third bytes contain HWID. It's encoded in big endian so
	 * convert it cpu endianness.
	 */
	hwid = sys_be16_to_cpu(*(uint16_t *)&hwid_cmd_buf[1]);

	/*
	 * Last 4 bits of hardware id is a year of sensor production,
	 * could differ between sensors.
	 */
	zassert_equal(hwid >> 4, 0x140);
}

ZTEST_F(fpc1145, test_pal_wait_irq)
{
	/* TODO: b/72360575 */
	zassert_equal(fpc1145_pal_wait_irq(NULL, IRQ_INT_TRIG), 0);
}

ZTEST_F(fpc1145, test_pal_get_time)
{
	uint64_t time_us;
	fpc1145_pal_get_time(&time_us);
	zassert_equal(time_us, k_ticks_to_us_near64(k_uptime_ticks()));
}

ZTEST_F(fpc1145, test_pal_delay_us)
{
	uint64_t us = 30000;
	uint64_t tick_us = (1000 * 1000) / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
	uint64_t t1, t2;

	fpc1145_pal_get_time(&t1);
	fpc1145_pal_delay_us(us);
	fpc1145_pal_get_time(&t2);

	zassert_within(t2 - t1, us, tick_us);
}

ZTEST_F(fpc1145, test_convert_fp_capture_mode_to_fpc_get_image_type)
{
	zassert_equal(convert_fp_capture_mode_to_fpc_get_image_type(
			      FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT),
		      FPC_CAPTURE_VENDOR_FORMAT);
	zassert_equal(convert_fp_capture_mode_to_fpc_get_image_type(
			      FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE),
		      FPC_CAPTURE_SIMPLE_IMAGE);
	zassert_equal(convert_fp_capture_mode_to_fpc_get_image_type(
			      FINGERPRINT_CAPTURE_TYPE_PATTERN0),
		      FPC_CAPTURE_PATTERN0);
	zassert_equal(convert_fp_capture_mode_to_fpc_get_image_type(
			      FINGERPRINT_CAPTURE_TYPE_PATTERN1),
		      FPC_CAPTURE_PATTERN1);
	zassert_equal(convert_fp_capture_mode_to_fpc_get_image_type(
			      FINGERPRINT_CAPTURE_TYPE_QUALITY_TEST),
		      FPC_CAPTURE_QUALITY_TEST);
	zassert_equal(convert_fp_capture_mode_to_fpc_get_image_type(
			      FINGERPRINT_CAPTURE_TYPE_RESET_TEST),
		      FPC_CAPTURE_RESET_TEST);
	zassert_equal(convert_fp_capture_mode_to_fpc_get_image_type(
			      FINGERPRINT_CAPTURE_TYPE_MAX),
		      -EINVAL);
}

ZTEST_F(fpc1145, test_acquire_image_small_buffer_size)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE] = { 0 };
	size_t image_buf_size = CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE - 1;
	int mode = FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT;

	zassert_equal(fingerprint_acquire_image(fixture->dev, mode, buffer,
						image_buf_size),
		      -EINVAL);
}

ZTEST_F(fpc1145, test_acquire_image_wrong_capture_mode)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE] = { 0 };
	int mode = FINGERPRINT_CAPTURE_TYPE_MAX;

	zassert_equal(fingerprint_acquire_image(fixture->dev, mode, buffer,
						sizeof(buffer)),
		      -EINVAL);
}
