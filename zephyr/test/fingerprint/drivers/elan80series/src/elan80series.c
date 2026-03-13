// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "elan80series_pal_test_helpers.h"

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
#include <emul/emul_elan80series.h>
#include <fingerprint/v4l2_types.h>
#include <fingerprint_elan80series.h>
#include <fingerprint_elan80series_private.h>

DEFINE_FFF_GLOBALS;

struct elan80series_fixture {
	const struct device *dev;
	const struct emul *target;
};

static void *elan80series_setup(void)
{
	static struct elan80series_fixture fixture = {
		.dev = DEVICE_DT_GET(DT_NODELABEL(elan80series)),
		.target = EMUL_DT_GET(DT_NODELABEL(elan80series)),
	};

	zassert_not_null(fixture.dev);
	zassert_not_null(fixture.target);
	return &fixture;
}

/* Converts capture types from the ec domain to the fpc domain. */
enum elan_capture_type convert_fp_capture_type_to_elan_capture_type(
	enum fingerprint_capture_type mode);

#define ELAN80SERIES_IMAGE_FRAME_PARAM_INITIALIZER(idx, node_id)            \
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
		NUM_IMAGE_CAPTURE_TYPES,
		ELAN80SERIES_IMAGE_FRAME_PARAM_INITIALIZER, (, ),
		DT_NODELABEL(elan80series)) };

ZTEST_SUITE(elan80series, NULL, elan80series_setup, NULL, NULL, NULL);

ZTEST_F(elan80series, test_convert_fp_capture_type_to_elan_capture_type)
{
	zassert_equal(convert_fp_capture_type_to_elan_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT),
		      ELAN_CAPTURE_VENDOR_FORMAT);
	zassert_equal(convert_fp_capture_type_to_elan_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE),
		      ELAN_CAPTURE_SIMPLE_IMAGE);
	zassert_equal(convert_fp_capture_type_to_elan_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_PATTERN0),
		      ELAN_CAPTURE_PATTERN0);
	zassert_equal(convert_fp_capture_type_to_elan_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_PATTERN1),
		      ELAN_CAPTURE_PATTERN1);
	zassert_equal(convert_fp_capture_type_to_elan_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_QUALITY_TEST),
		      ELAN_CAPTURE_QUALITY_TEST);
	zassert_equal(convert_fp_capture_type_to_elan_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_RESET_TEST),
		      ELAN_CAPTURE_RESET_TEST);
	zassert_equal(convert_fp_capture_type_to_elan_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_MAX),
		      ELAN_CAPTURE_TYPE_INVALID);
}

ZTEST_F(elan80series, test_init_success)
{
	zassert_ok(fingerprint_init(fixture->dev));
}

ZTEST_F(elan80series, test_init_failure_bad_hwid)
{
	elan80series_set_hwid(fixture->target, 0x0, 0x0);
	zassert_equal(fingerprint_init(fixture->dev), -ENOTSUP);
}

ZTEST_F(elan80series, test_init_failure_spi)
{
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES];
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES;

	elan80series_stop_spi(fixture->target);
	zassert_equal(fingerprint_init(fixture->dev), -ENOTSUP);
	zassert_not_ok(fingerprint_get_info(fixture->dev, &sensor_info,
					    image_frame_params_array,
					    &num_params));
	elan80series_start_spi(fixture->target);
	zassert_ok(fingerprint_get_info(fixture->dev, &sensor_info,
					image_frame_params_array, &num_params));
	zassert_equal(sensor_info.errors,
		      (FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN |
		       FINGERPRINT_ERROR_SPI_COMM | FINGERPRINT_ERROR_BAD_HWID |
		       FINGERPRINT_ERROR_INIT_FAIL));
}

ZTEST_F(elan80series, test_get_info)
{
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params
		image_frame_params_array[NUM_IMAGE_CAPTURE_TYPES];
	uint8_t num_params = NUM_IMAGE_CAPTURE_TYPES;

	/* We need to initialize driver first to initialize 'error' field */
	zassert_ok(fingerprint_init(fixture->dev));
	zassert_ok(fingerprint_get_info(fixture->dev, &sensor_info,
					image_frame_params_array, &num_params));
	zassert_equal(
		num_params, NUM_IMAGE_CAPTURE_TYPES,
		"fingerprint_get_info returned an unexpected number of params");

#if defined(CONFIG_FINGERPRINT_SENSOR_ELAN80SG)
	zassert_equal(sensor_info.product_id, 0x0903);
	zassert_equal(sensor_info.model_id, 0x4f4f);
	zassert_equal(sensor_info.version, 0x100B);
#endif
	zassert_equal(sensor_info.vendor_id, FOURCC('E', 'L', 'A', 'N'));
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

ZTEST_F(elan80series, test_deinit_success)
{
	zassert_ok(fingerprint_deinit(fixture->dev));
}

ZTEST_F(elan80series, test_enter_idle)
{
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_IDLE));
}

ZTEST_F(elan80series, test_invalid_mode_not_supported)
{
	zassert_equal(fingerprint_set_mode(fixture->dev, UINT16_MAX), -ENOTSUP);
}

ZTEST_F(elan80series, test_maintenance_image_small_buffer_size)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE - 1];

	zassert_equal(fingerprint_maintenance(fixture->dev, buffer,
					      sizeof(buffer)),
		      -EINVAL);
}

ZTEST_F(elan80series, test_maintenance_not_supported)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_maintenance(fixture->dev, buffer,
					      sizeof(buffer)),
		      0);
}

ZTEST_F(elan80series, test_finger_status_not_supported)
{
	zassert_equal(fingerprint_finger_status(fixture->dev), -ENOTSUP);
}

FAKE_VOID_FUNC(test_interrupt_handler, const struct device *);

ZTEST_F(elan80series, test_interrupt)
{
	const struct gpio_dt_spec spec =
		GPIO_DT_SPEC_GET(DT_NODELABEL(elan80series), irq_gpios);

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

ZTEST_F(elan80series, test_acquire_image_not_supported)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_acquire_image(fixture->dev, 0, buffer,
						sizeof(buffer)),
		      -ENOTSUP);
}

ZTEST_F(elan80series, test_acquire_image_small_buffer_size)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE] = { 0 };
	size_t image_buf_size = CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE - 1;
	enum fingerprint_capture_type capture_type =
		FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT;

	zassert_equal(fingerprint_acquire_image(fixture->dev, capture_type,
						buffer, image_buf_size),
		      -EINVAL);
}

ZTEST_F(elan80series, test_acquire_image_wrong_capture_type)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE] = { 0 };
	enum fingerprint_capture_type capture_type =
		FINGERPRINT_CAPTURE_TYPE_MAX;

	zassert_equal(fingerprint_acquire_image(fixture->dev, capture_type,
						buffer, sizeof(buffer)),
		      -EINVAL);
}

ZTEST_F(elan80series, test_elan_read_register)
{
	uint8_t hwid_lo_cmd = 0x04;
	uint8_t hwid_hi_cmd = 0x02;
	uint8_t regdata = 0;
	uint8_t expected_hwid_lo_regdata = 0x4F;
	uint8_t expected_hwid_hi_regdata = 0x4F;

	zassert_ok(elan80series_pal_read_register(hwid_lo_cmd, &regdata));
	zassert_equal(expected_hwid_lo_regdata, regdata);

	zassert_ok(elan80series_pal_read_register(hwid_hi_cmd, &regdata));
	zassert_equal(expected_hwid_hi_regdata, regdata);
}

ZTEST_F(elan80series, test_elan_read_register_spi_stoppped)
{
	uint8_t hwid_lo_cmd = 0x04;
	uint8_t regdata = 0;

	elan80series_stop_spi(fixture->target);
	zassert_equal(elan80series_pal_read_register(hwid_lo_cmd, &regdata),
		      -EIO);
}

ZTEST_F(elan80series, test_elan_read_cmd)
{
	uint8_t cmd_hwid_lo_cmd = 0x44;
	uint8_t cmd_hwid_hi_cmd = 0x42;
	uint8_t regdata = 0;
	uint8_t expected_hwid_lo_regdata = 0x4F;
	uint8_t expected_hwid_hi_regdata = 0x4F;

	zassert_ok(elan80series_pal_read_cmd(cmd_hwid_lo_cmd, &regdata));
	zassert_equal(expected_hwid_lo_regdata, regdata);

	zassert_ok(elan80series_pal_read_cmd(cmd_hwid_hi_cmd, &regdata));
	zassert_equal(expected_hwid_hi_regdata, regdata);
}

ZTEST_F(elan80series, test_elan_read_cmd_spi_stoppped)
{
	uint8_t cmd_hwid_lo_cmd = 0x44;
	uint8_t regdata = 0;

	elan80series_stop_spi(fixture->target);
	zassert_equal(elan80series_pal_read_cmd(cmd_hwid_lo_cmd, &regdata),
		      -EIO);
}

ZTEST_F(elan80series, test_elan_usleep)
{
	uint64_t usecs = 30000;
	uint64_t tick_usecs = USEC_PER_SEC / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
	uint64_t t1, t2;

	/* k_uptime_get() returns system uptime in milliseconds. */
	t1 = k_uptime_get();
	zassert_ok(elan80series_pal_usleep(usecs));
	t2 = k_uptime_get();

	zassert_within((t2 - t1) * USEC_PER_MSEC, usecs, tick_usecs);
}

ZTEST_F(elan80series, test_pal_get_tick)
{
	zassert_equal(elan80series_pal_get_tick(), k_uptime_get_32());
}

ZTEST_F(elan80series, test_elan_malloc)
{
	size_t size = sizeof(int);
	void *void_ptr = elan80series_pal_malloc(size);
	zassert_not_null(void_ptr, "sys_alloc should return a valid pointer");

	int *int_ptr = (int *)void_ptr;
	int num = 4546;
	*int_ptr = num;
	zassert_equal(*int_ptr, num);

	elan80series_pal_free(void_ptr);
}

static ZTEST_DMEM volatile int expected_reason = -1;

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *pEsf)
{
	printk("Caught system error -- reason %d\n", reason);

	zassert_not_equal(expected_reason, -1, "Unexpected crash");
	zassert_equal(reason, expected_reason,
		      "Wrong crash type got %d expected %d\n", reason,
		      expected_reason);

	expected_reason = -1;
	ztest_test_pass();
}

ZTEST_F(elan80series, test_pal_oops_on_memory_alloc_fail)
{
	expected_reason = K_ERR_KERNEL_OOPS;
	elan80series_pal_malloc(
		CONFIG_FINGERPRINT_SENSOR_ELAN80SERIES_HEAP_SIZE);

	ztest_test_fail();
}

ZTEST_F(elan80series, test_pal_reset_pin)
{
	const struct gpio_dt_spec reset_pin =
		GPIO_DT_SPEC_GET(DT_NODELABEL(elan80series), reset_gpios);

	/* Reset is active when GPIO output is low. */
	elan80series_pal_sensor_set_rst(true);
	zassert_equal(gpio_emul_output_get(reset_pin.port, reset_pin.pin), 0);

	/* Reset is inactive when GPIO output is high. */
	elan80series_pal_sensor_set_rst(false);
	zassert_equal(gpio_emul_output_get(reset_pin.port, reset_pin.pin), 1);
}
