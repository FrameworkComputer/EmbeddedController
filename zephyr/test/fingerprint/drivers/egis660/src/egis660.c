/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "egis660_pal_test_helpers.h"

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
#include <emul/emul_egis660.h>
#include <fingerprint/v4l2_types.h>
#include <fingerprint_egis660.h>
#include <fingerprint_egis660_private.h>

DEFINE_FFF_GLOBALS;

struct egis660_fixture {
	const struct device *dev;
	const struct emul *target;
};

static void *egis660_setup(void)
{
	static struct egis660_fixture fixture = {
		.dev = DEVICE_DT_GET(DT_NODELABEL(egis660)),
		.target = EMUL_DT_GET(DT_NODELABEL(egis660)),
	};

	zassert_not_null(fixture.dev);
	zassert_not_null(fixture.target);
	return &fixture;
}

/* Converts capture types from the ec domain to the egis domain. */
int convert_fp_capture_type_to_egis_capture_type(int mode);

#define EGIS660_IMAGE_FRAME_PARAM_INITIALIZER(idx, node_id)                 \
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
		NUM_IMAGE_CAPTURE_TYPES, EGIS660_IMAGE_FRAME_PARAM_INITIALIZER,
		(, ), DT_NODELABEL(egis660)) };

ZTEST_SUITE(egis660, NULL, egis660_setup, NULL, NULL, NULL);

ZTEST_F(egis660, test_init_success)
{
	zassert_ok(fingerprint_init(fixture->dev));
}

ZTEST_F(egis660, test_deinit_success)
{
	zassert_ok(fingerprint_deinit(fixture->dev));
}

ZTEST_F(egis660, test_get_info)
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

	zassert_equal(sensor_info.vendor_id, FOURCC('E', 'G', 'I', 'S'));
	zassert_equal(sensor_info.product_id, 9);
	zassert_equal(sensor_info.version, 1);

	for (int i = 0; i < NUM_IMAGE_CAPTURE_TYPES; ++i) {
		zassert_equal(
			memcmp(&image_frame_params_array[i],
			       &expected_image_frame_params_array[i],
			       sizeof(struct fingerprint_image_frame_params)),
			0, "Struct comparison failed at index %d", i);
	}
}

ZTEST_F(egis660, test_enter_idle)
{
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_IDLE));
}

ZTEST_F(egis660, test_invalid_mode_not_supported)
{
	zassert_equal(fingerprint_set_mode(fixture->dev, UINT16_MAX), -ENOTSUP);
}

FAKE_VOID_FUNC(test_interrupt_handler, const struct device *);

ZTEST_F(egis660, test_interrupt)
{
	const struct gpio_dt_spec spec =
		GPIO_DT_SPEC_GET(DT_NODELABEL(egis660), irq_gpios);

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

ZTEST_F(egis660, test_maintenance_not_supported)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_maintenance(fixture->dev, buffer,
					      sizeof(buffer)),
		      -ENOTSUP);
}

ZTEST_F(egis660, test_finger_status_not_supported)
{
	zassert_equal(fingerprint_finger_status(fixture->dev), -ENOTSUP);
}

ZTEST_F(egis660, test_acquire_image_not_supported)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_acquire_image(fixture->dev, 0, buffer,
						sizeof(buffer)),
		      -ENOTSUP);
}

ZTEST_F(egis660, test_sensor_mode_detect_not_supported)
{
	zassert_equal(fingerprint_set_mode(fixture->dev,
					   FINGERPRINT_SENSOR_MODE_DETECT),
		      -ENOTSUP);
}

ZTEST_F(egis660, test_pal_spi_write_read)
{
	/*
	 * Prepare buffer with command and place for response.
	 * Sensor should return hardware id.
	 */
	uint8_t hwid_cmd_buf[] = { 0xFC, 0x00, 0x00 };
	uint16_t hwid;

	zassert_ok(egis660_pal_spi_write_read(hwid_cmd_buf, 1, 2, false));
	/*
	 * Second and third bytes contain HWID. It's encoded in big endian so
	 * convert it cpu endianness.
	 */
	hwid = sys_be16_to_cpu(*(uint16_t *)&hwid_cmd_buf[1]);

	/*
	 * Last 4 bits of hardware id is a year of sensor production,
	 * could differ between sensors.
	 */
	zassert_equal(hwid >> 4, 0x2d1);
}

ZTEST_F(egis660, test_pal_spi_write_read_series)
{
	uint8_t hwid_cmd_buf[] = { 0xFC, 0x00, 0x00 };

	zassert_ok(egis660_pal_spi_write_read(hwid_cmd_buf, 1, 2, true));
	/* The second transfer is done, with the CS already asserted */
	zassert_ok(egis660_pal_spi_write_read(hwid_cmd_buf, 1, 2, false));
}

ZTEST_F(egis660, test_pal_check_irq)
{
	const struct gpio_dt_spec irq_pin =
		GPIO_DT_SPEC_GET(DT_NODELABEL(egis660), irq_gpios);

	gpio_emul_input_set(irq_pin.port, irq_pin.pin, 1);
	zassert_equal(egis660_pal_check_irq(), true);
	gpio_emul_input_set(irq_pin.port, irq_pin.pin, 0);
	zassert_equal(egis660_pal_check_irq(), false);
}

ZTEST_F(egis660, test_pal_read_irq)
{
	const struct gpio_dt_spec irq_pin =
		GPIO_DT_SPEC_GET(DT_NODELABEL(egis660), irq_gpios);

	gpio_emul_input_set(irq_pin.port, irq_pin.pin, 1);
	zassert_equal(egis660_pal_read_irq(), true);
	gpio_emul_input_set(irq_pin.port, irq_pin.pin, 0);
	zassert_equal(egis660_pal_read_irq(), false);
}

ZTEST_F(egis660, test_pal_reset_pin)
{
	const struct gpio_dt_spec reset_pin =
		GPIO_DT_SPEC_GET(DT_NODELABEL(egis660), reset_gpios);

	/* Reset is active when GPIO output is low. */
	egis660_pal_reset(true);
	zassert_equal(gpio_emul_output_get(reset_pin.port, reset_pin.pin), 0);

	/* Reset is inactive when GPIO output is high. */
	egis660_pal_reset(false);
	zassert_equal(gpio_emul_output_get(reset_pin.port, reset_pin.pin), 1);
}

ZTEST_F(egis660, test_pal_timebase_get_tick)
{
	zassert_equal(egis660_pal_timebase_get_tick(), k_uptime_get_32());
}

ZTEST_F(egis660, test_pal_timebase_delay_ms)
{
	uint32_t t1, t2;

	t1 = egis660_pal_timebase_get_tick();

	/* Wait 100ms. */
	egis660_pal_timebase_delay_ms(100);

	t2 = egis660_pal_timebase_get_tick();

	/* Add some margin of 10ms */
	zassert_true((t2 - t1) >= 90 && (t2 - t1) <= 110);
}

ZTEST_F(egis660, test_pal_memory_alloc)
{
	void *p;

	p = egis660_pal_malloc(2048);
	zassert_not_null(p);

	egis660_pal_free(p);
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

ZTEST_F(egis660, test_convert_fp_capture_type_to_egis_capture_type)
{
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT),
		      EGIS_CAPTURE_VENDOR_FORMAT);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_SIMPLE_IMAGE),
		      EGIS_CAPTURE_SIMPLE_IMAGE);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_PATTERN0),
		      EGIS_CAPTURE_PATTERN0);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_PATTERN1),
		      EGIS_CAPTURE_PATTERN1);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_RESET_TEST),
		      EGIS_CAPTURE_RESET_TEST);
	zassert_equal(convert_fp_capture_type_to_egis_capture_type(
			      FINGERPRINT_CAPTURE_TYPE_MAX),
		      -EINVAL);
}

ZTEST_F(egis660, test_acquire_image_small_buffer_size)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE] = { 0 };
	size_t image_buf_size = CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE - 1;
	enum fingerprint_capture_type capture_type =
		FINGERPRINT_CAPTURE_TYPE_VENDOR_FORMAT;

	zassert_equal(fingerprint_acquire_image(fixture->dev, capture_type,
						buffer, image_buf_size),
		      -EINVAL);
}

ZTEST_F(egis660, test_acquire_image_wrong_capture_type)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE] = { 0 };
	enum fingerprint_capture_type capture_type =
		FINGERPRINT_CAPTURE_TYPE_MAX;

	zassert_equal(fingerprint_acquire_image(fixture->dev, capture_type,
						buffer, sizeof(buffer)),
		      -EINVAL);
}
