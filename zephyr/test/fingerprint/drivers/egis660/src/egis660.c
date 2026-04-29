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

ZTEST_F(egis660, test_pal_timebase_delay_us)
{
	uint32_t t1, t2;

	t1 = egis660_pal_timebase_get_tick();

	/* Wait 10000us. */
	egis660_pal_timebase_delay_us(10000);

	t2 = egis660_pal_timebase_get_tick();

	/* Add some margin of 1ms */
	zassert_true((t2 - t1) >= 9 && (t2 - t1) <= 11);
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

/* Flash test helpers — simulate APNS flash operations */

ZTEST_F(egis660, test_flash_erase)
{
	int rc;

	rc = egis660_pal_flash_erase(0, CONFIG_FLASH_ERASE_SIZE);
	zassert_ok(rc, "flash_erase failed: %d", rc);

	/* Verify erased content */
	uint8_t buf[4];

	rc = egis660_pal_flash_read(0, sizeof(buf), buf);
	zassert_ok(rc, "flash_read after erase failed: %d", rc);
	zassert_equal(buf[0], 0xff, "flash should be erased to 0xff");
	zassert_equal(buf[1], 0xff, "flash should be erased to 0xff");
	zassert_equal(buf[2], 0xff, "flash should be erased to 0xff");
	zassert_equal(buf[3], 0xff, "flash should be erased to 0xff");
}

ZTEST_F(egis660, test_flash_write_and_read)
{
	int rc;

	/* Erase first */
	rc = egis660_pal_flash_erase(0, CONFIG_FLASH_ERASE_SIZE);
	zassert_ok(rc, "flash_erase failed: %d", rc);

	/* Write test data - must be aligned to CONFIG_FLASH_WRITE_IDEAL_SIZE */
	const uint8_t test_data[CONFIG_FLASH_WRITE_IDEAL_SIZE] = {
		0x12, 0x34, 0x56, 0x78, 0xAB, 0xCD, 0xEF, 0x01
	};

	rc = egis660_pal_flash_write(0, test_data, sizeof(test_data));
	zassert_ok(rc, "flash_write failed: %d", rc);

	/* Read back and verify */
	uint8_t read_buf[sizeof(test_data)] = { 0 };

	rc = egis660_pal_flash_read(0, sizeof(read_buf), read_buf);
	zassert_ok(rc, "flash_read failed: %d", rc);
	zassert_mem_equal(read_buf, test_data, sizeof(test_data),
			  "read data mismatch");
}

ZTEST_F(egis660, test_flash_write_offset)
{
	int rc;

	rc = egis660_pal_flash_erase(0, CONFIG_FLASH_ERASE_SIZE);
	zassert_ok(rc, "flash_erase failed: %d", rc);

	/* Write at a non-zero offset - aligned to CONFIG_FLASH_WRITE_IDEAL_SIZE
	 */
	const uint8_t test_data[CONFIG_FLASH_WRITE_IDEAL_SIZE] = { 0xDE, 0xAD,
								   0xBE, 0xEF };
	const uint32_t offset = CONFIG_FLASH_WRITE_IDEAL_SIZE;

	rc = egis660_pal_flash_write(offset, test_data, sizeof(test_data));
	zassert_ok(rc, "flash_write at offset failed: %d", rc);

	/* Read back */
	uint8_t read_buf[sizeof(test_data)] = { 0 };

	rc = egis660_pal_flash_read(offset, sizeof(read_buf), read_buf);
	zassert_ok(rc, "flash_read at offset failed: %d", rc);
	zassert_mem_equal(read_buf, test_data, sizeof(test_data),
			  "value at offset 0x%x mismatch", offset);
}

ZTEST_F(egis660, test_flash_out_of_bounds)
{
	uint8_t buf[4];

	/* Read/write/erase beyond partition size should fail */
	zassert_not_ok(egis660_pal_flash_read(APNS_TEST_SIZE, 4, buf));
	zassert_not_ok(egis660_pal_flash_write(APNS_TEST_SIZE, buf, 4));
	zassert_not_ok(egis660_pal_flash_erase(APNS_TEST_SIZE,
					       CONFIG_FLASH_ERASE_SIZE));

	/* Write spanning beyond partition boundary */
	zassert_not_ok(egis660_pal_flash_write(APNS_TEST_SIZE - 2, buf, 4));
}

ZTEST_F(egis660, test_flash_null_buffer)
{
	zassert_not_ok(egis660_pal_flash_read(0, 4, NULL));
	zassert_not_ok(egis660_pal_flash_write(0, NULL, 4));
}

ZTEST_F(egis660, test_flash_overwrite)
{
	int rc;

	/* Erase and write initial data - aligned to
	 * CONFIG_FLASH_WRITE_IDEAL_SIZE */
	rc = egis660_pal_flash_erase(0, CONFIG_FLASH_ERASE_SIZE);
	zassert_ok(rc, "flash_erase failed: %d", rc);

	const uint8_t data1[CONFIG_FLASH_WRITE_IDEAL_SIZE] = { 0xAA };

	rc = egis660_pal_flash_write(0, data1, sizeof(data1));
	zassert_ok(rc, "flash_write data1 failed: %d", rc);

	/* Erase and write different data at same location */
	rc = egis660_pal_flash_erase(0, CONFIG_FLASH_ERASE_SIZE);
	zassert_ok(rc, "flash_erase second failed: %d", rc);

	const uint8_t data2[CONFIG_FLASH_WRITE_IDEAL_SIZE] = { 0x55 };

	rc = egis660_pal_flash_write(0, data2, sizeof(data2));
	zassert_ok(rc, "flash_write data2 failed: %d", rc);

	/* Verify data2 overwrote data1 */
	uint8_t read_buf[sizeof(data2)] = { 0 };

	rc = egis660_pal_flash_read(0, sizeof(read_buf), read_buf);
	zassert_ok(rc, "flash_read after overwrite failed: %d", rc);
	zassert_mem_equal(read_buf, data2, sizeof(data2),
			  "overwrite verification failed");
}
