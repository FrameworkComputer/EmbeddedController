/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpc1025_pal_test_helpers.h"

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
#include <emul/emul_fpc1025.h>
#include <fingerprint/v4l2_types.h>

DEFINE_FFF_GLOBALS;

struct fpc1025_fixture {
	const struct device *dev;
	const struct emul *target;
};

static void *fpc1025_setup(void)
{
	static struct fpc1025_fixture fixture = {
		.dev = DEVICE_DT_GET(DT_NODELABEL(fpc1025)),
		.target = EMUL_DT_GET(DT_NODELABEL(fpc1025)),
	};

	zassert_not_null(fixture.dev);
	zassert_not_null(fixture.target);
	return &fixture;
}

ZTEST_SUITE(fpc1025, NULL, fpc1025_setup, NULL, NULL, NULL);

ZTEST_F(fpc1025, test_init_success)
{
	zassert_ok(fingerprint_init(fixture->dev));
	zassert_equal(fpc1025_get_low_power_mode(fixture->target), 1);
}

ZTEST_F(fpc1025, test_init_failure_bad_hwid)
{
	fpc1025_set_hwid(fixture->target, 0x0);
	zassert_equal(fingerprint_init(fixture->dev), -EINVAL);
}

ZTEST_F(fpc1025, test_deinit_success)
{
	zassert_ok(fingerprint_deinit(fixture->dev));
}

ZTEST_F(fpc1025, test_get_info)
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
	zassert_equal(info.model_id >> 4, 0x021);
	zassert_equal(info.version, 1);
	zassert_equal(info.frame_size, CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE);
	zassert_equal(info.pixel_format, FINGERPRINT_SENSOR_V4L2_PIXEL_FORMAT(
						 DT_NODELABEL(fpc1025)));
	zassert_equal(info.width,
		      FINGERPRINT_SENSOR_RES_X(DT_NODELABEL(fpc1025)));
	zassert_equal(info.height,
		      FINGERPRINT_SENSOR_RES_Y(DT_NODELABEL(fpc1025)));
	zassert_equal(info.bpp,
		      FINGERPRINT_SENSOR_RES_BPP(DT_NODELABEL(fpc1025)));
	zassert_equal(info.errors, FINGERPRINT_ERROR_DEAD_PIXELS_UNKNOWN);
}

ZTEST_F(fpc1025, test_enter_low_power_mode)
{
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_LOW_POWER));
	zassert_equal(fpc1025_get_low_power_mode(fixture->target), 1);
}

ZTEST_F(fpc1025, test_enter_idle)
{
	zassert_ok(fingerprint_set_mode(fixture->dev,
					FINGERPRINT_SENSOR_MODE_IDLE));
}

ZTEST_F(fpc1025, test_invalid_mode_not_supported)
{
	zassert_equal(fingerprint_set_mode(fixture->dev, UINT16_MAX), -ENOTSUP);
}

FAKE_VOID_FUNC(test_interrupt_handler, const struct device *);

ZTEST_F(fpc1025, test_interrupt)
{
	const struct gpio_dt_spec spec =
		GPIO_DT_SPEC_GET(DT_NODELABEL(fpc1025), irq_gpios);

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

ZTEST_F(fpc1025, test_maintenance_not_supported)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_maintenance(fixture->dev, buffer,
					      sizeof(buffer)),
		      -ENOTSUP);
}

ZTEST_F(fpc1025, test_finger_status_not_supported)
{
	zassert_equal(fingerprint_finger_status(fixture->dev), -ENOTSUP);
}

ZTEST_F(fpc1025, test_acquire_image_not_supported)
{
	uint8_t buffer[CONFIG_FINGERPRINT_SENSOR_IMAGE_SIZE];

	zassert_equal(fingerprint_acquire_image(fixture->dev, 0, buffer,
						sizeof(buffer)),
		      -ENOTSUP);
}

ZTEST_F(fpc1025, test_sensor_mode_detect_not_supported)
{
	zassert_equal(fingerprint_set_mode(fixture->dev,
					   FINGERPRINT_SENSOR_MODE_DETECT),
		      -ENOTSUP);
}

ZTEST_F(fpc1025, test_pal_spi_write_read)
{
	/*
	 * Prepare buffer with command and place for response.
	 * Sensor should return hardware id.
	 */
	uint8_t hwid_cmd_buf[] = { 0xFC, 0x00, 0x00 };
	uint16_t hwid;

	zassert_ok(fpc1025_pal_spi_write_read(hwid_cmd_buf, hwid_cmd_buf,
					      sizeof(hwid_cmd_buf), false));
	/*
	 * Second and third bytes contain HWID. It's encoded in big endian so
	 * convert it cpu endianness.
	 */
	hwid = sys_be16_to_cpu(*(uint16_t *)&hwid_cmd_buf[1]);

	/*
	 * Last 4 bits of hardware id is a year of sensor production,
	 * could differ between sensors.
	 */
	zassert_equal(hwid >> 4, 0x021);
}

ZTEST_F(fpc1025, test_pal_check_irq)
{
	const struct gpio_dt_spec irq_pin =
		GPIO_DT_SPEC_GET(DT_NODELABEL(fpc1025), irq_gpios);

	gpio_emul_input_set(irq_pin.port, irq_pin.pin, 1);
	zassert_equal(fpc1025_pal_spi_check_irq(), true);
	gpio_emul_input_set(irq_pin.port, irq_pin.pin, 0);
	zassert_equal(fpc1025_pal_spi_check_irq(), false);
}

ZTEST_F(fpc1025, test_pal_read_irq)
{
	const struct gpio_dt_spec irq_pin =
		GPIO_DT_SPEC_GET(DT_NODELABEL(fpc1025), irq_gpios);

	gpio_emul_input_set(irq_pin.port, irq_pin.pin, 1);
	zassert_equal(fpc1025_pal_spi_read_irq(), true);
	gpio_emul_input_set(irq_pin.port, irq_pin.pin, 0);
	zassert_equal(fpc1025_pal_spi_read_irq(), false);
}

ZTEST_F(fpc1025, test_pal_reset_pin)
{
	const struct gpio_dt_spec reset_pin =
		GPIO_DT_SPEC_GET(DT_NODELABEL(fpc1025), reset_gpios);

	/* Reset is active when GPIO output is low. */
	fpc1025_pal_spi_reset(true);
	zassert_equal(gpio_emul_output_get(reset_pin.port, reset_pin.pin), 0);

	/* Reset is inactive when GPIO output is high. */
	fpc1025_pal_spi_reset(false);
	zassert_equal(gpio_emul_output_get(reset_pin.port, reset_pin.pin), 1);
}

ZTEST_F(fpc1025, test_pal_timebase_get_tick)
{
	zassert_equal(fpc1025_pal_timebase_get_tick(), k_uptime_get_32());
}

ZTEST_F(fpc1025, test_pal_timebase_busy_wait)
{
	uint32_t t1, t2;

	t1 = fpc1025_pal_timebase_get_tick();

	/* Wait 100ms. */
	fpc1025_pal_timebase_busy_wait(100);

	t2 = fpc1025_pal_timebase_get_tick();

	zassert_true((t2 - t1) == 100);
}

ZTEST_F(fpc1025, test_pal_memory_alloc)
{
	void *p;

	p = fpc1025_pal_malloc(2048);
	zassert_not_null(p);

	fpc1025_pal_free(p);
}

static ZTEST_DMEM volatile int expected_reason = -1;

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *pEsf)
{
	printk("Caught system error -- reason %d\n", reason);

	zassert_not_equal(expected_reason, -1, "Unexpected crash");
	zassert_equal(reason, expected_reason,
		      "Wrong crash type got %d expected %d\n", reason,
		      expected_reason);

	expected_reason = -1;
	ztest_test_pass();
}

ZTEST_F(fpc1025, test_pal_oops_on_memory_alloc_fail)
{
	expected_reason = K_ERR_KERNEL_OOPS;
	fpc1025_pal_malloc(CONFIG_FINGERPRINT_SENSOR_FPC1025_HEAP_SIZE);

	ztest_test_fail();
}
