/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "ap_power/ap_power.h"
#include "ap_power/ap_power_events.h"
#include "drivers/one_wire_uart.h"
#include "drivers/one_wire_uart_internal.h"
#include "gpio_signal.h"
#include "keyboard_config.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_hid_touchpad.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest_assert.h>

const static struct device *dev = DEVICE_DT_GET(DT_NODELABEL(hid_i2c_target));

ZTEST(hid_i2c_touchpad, test_reset_flow)
{
	const struct gpio_dt_spec *hid_irq =
		GPIO_DT_FROM_NODELABEL(gpio_ec_ap_hid_int_odl);
	struct i2c_target_data *data = dev->data;
	struct i2c_target_config *target_cfg = &data->config;
	const struct i2c_target_callbacks *callbacks = target_cfg->callbacks;
	uint8_t cmd[] = { 0x5, 0x00, 0x00, 0x01 }; /* reset command */
	uint8_t *read_ptr;
	uint32_t read_len;

	callbacks->buf_write_received(target_cfg, cmd, sizeof(cmd));
	callbacks->stop(target_cfg);

	/* 7.2.1.2 At the end of the reset, the DEVICE must also write a 2 Byte
	 * value to the Input Register with the sentinel value of 0x0000
	 * (2 Bytes containing 0) and must assert the Interrupt to indicate that
	 * it has been initialized
	 */
	zassert_equal(gpio_emul_output_get(hid_irq->port, hid_irq->pin), 0);
	zassert_equal(data->in_reset, true);

	callbacks->buf_read_requested(target_cfg, &read_ptr, &read_len);
	callbacks->stop(target_cfg);
	zassert_equal(read_len, 2);
	zassert_equal(read_ptr[0], 0);
	zassert_equal(read_ptr[1], 0);
	zassert_equal(gpio_emul_output_get(hid_irq->port, hid_irq->pin), 1);
}

ZTEST(hid_i2c_touchpad, test_touchpad_event)
{
	const struct gpio_dt_spec *hid_irq =
		GPIO_DT_FROM_NODELABEL(gpio_ec_ap_hid_int_odl);
	struct i2c_target_data *data = dev->data;
	struct i2c_target_config *target_cfg = &data->config;
	const struct i2c_target_callbacks *callbacks = target_cfg->callbacks;
	uint8_t *read_ptr;
	uint32_t read_len;
	struct usb_hid_touchpad_report expected1, expected2;

	memset(&expected1, 0x56, sizeof(struct usb_hid_touchpad_report));
	memset(&expected2, 0x78, sizeof(struct usb_hid_touchpad_report));
	hid_i2c_touchpad_add(dev, &expected1);
	hid_i2c_touchpad_add(dev, &expected2);

	zassert_equal(gpio_emul_output_get(hid_irq->port, hid_irq->pin), 0);

	/* first read should return `expected1`, and irq keep asserted because
	 * there's another event queued
	 */
	callbacks->buf_read_requested(target_cfg, &read_ptr, &read_len);
	callbacks->stop(target_cfg);
	zassert_equal(read_len, sizeof(struct usb_hid_touchpad_report) + 2);
	zassert_equal(sys_get_le16(read_ptr),
		      sizeof(struct usb_hid_touchpad_report));
	zassert_mem_equal(&expected1, read_ptr + 2,
			  sizeof(struct usb_hid_touchpad_report));
	zassert_equal(gpio_emul_output_get(hid_irq->port, hid_irq->pin), 0);

	/* second read should return `expected2`, and deassert the irq */
	callbacks->buf_read_requested(target_cfg, &read_ptr, &read_len);
	callbacks->stop(target_cfg);
	zassert_equal(read_len, sizeof(struct usb_hid_touchpad_report) + 2);
	zassert_equal(sys_get_le16(read_ptr),
		      sizeof(struct usb_hid_touchpad_report));
	zassert_mem_equal(&expected2, read_ptr + 2,
			  sizeof(struct usb_hid_touchpad_report));
	zassert_equal(gpio_emul_output_get(hid_irq->port, hid_irq->pin), 1);
}

ZTEST(hid_i2c_touchpad, test_descriptor)
{
	struct i2c_target_data *data = dev->data;
	struct i2c_target_config *target_cfg = &data->config;
	const struct i2c_target_callbacks *callbacks = target_cfg->callbacks;
	uint8_t *read_ptr;
	uint32_t read_len;

	uint16_t hid_desc_reg = 1;
	/* read HID descriptor */
	callbacks->buf_write_received(target_cfg, (uint8_t *)&hid_desc_reg,
				      sizeof(hid_desc_reg));
	callbacks->buf_read_requested(target_cfg, &read_ptr, &read_len);
	callbacks->stop(target_cfg);

	/* HID descriptor is always 30 bytes */
	zassert_equal(read_len, 30);

	uint16_t report_desc_len = sys_get_le16(read_ptr + 4);
	uint16_t report_desc_reg = sys_get_le16(read_ptr + 6);

	callbacks->buf_write_received(target_cfg, (uint8_t *)&report_desc_reg,
				      sizeof(report_desc_reg));
	callbacks->buf_read_requested(target_cfg, &read_ptr, &read_len);
	callbacks->stop(target_cfg);
	/* verify that the length of return report equals to the
	 * `wReportDescRegister` field in the HID descriptor
	 */
	zassert_equal(read_len, report_desc_len);
}

ZTEST(hid_i2c_touchpad, test_bad_input)
{
	struct i2c_target_data *data = dev->data;
	struct i2c_target_config *target_cfg = &data->config;
	const struct i2c_target_callbacks *callbacks = target_cfg->callbacks;
	uint8_t *read_ptr;
	uint32_t read_len;
	uint8_t bad_input_1[] = { 0x5, 0x00 }; /* cmd register without data */
	uint8_t bad_input_2[] = { 0x99, 0x00 }; /* undefined register */

	callbacks->buf_write_received(target_cfg, bad_input_1,
				      sizeof(bad_input_1));
	callbacks->buf_read_requested(target_cfg, &read_ptr, &read_len);
	callbacks->stop(target_cfg);
	zassert_equal(0, read_len);

	callbacks->buf_write_received(target_cfg, bad_input_2,
				      sizeof(bad_input_1));
	callbacks->buf_read_requested(target_cfg, &read_ptr, &read_len);
	callbacks->stop(target_cfg);
	zassert_equal(0, read_len);
}

ZTEST(hid_i2c_touchpad, test_get_report)
{
	struct i2c_target_data *data = dev->data;
	struct i2c_target_config *target_cfg = &data->config;
	const struct i2c_target_callbacks *callbacks = target_cfg->callbacks;
	uint8_t get_report_request[] = {
		0x05, 0x00, /* CMD_REG */
		0x00, /* REPORT_ID (empty) */
		0x02, /* OP_CODE GET_REPORT */
		0x06, 0x00, /* DATA_REG */
	};
	uint8_t *read_ptr;
	uint32_t read_len;

	/* verify that the size and id in the response buffer is correct */
	get_report_request[2] = REPORT_ID_DEVICE_CERT;
	callbacks->buf_write_received(target_cfg, get_report_request,
				      sizeof(get_report_request));
	callbacks->buf_read_requested(target_cfg, &read_ptr, &read_len);
	callbacks->stop(target_cfg);
	zassert_equal(*(uint16_t *)read_ptr, 257);
	zassert_equal(read_ptr[2], REPORT_ID_DEVICE_CERT);

	get_report_request[2] = REPORT_ID_DEVICE_CAPS;
	callbacks->buf_write_received(target_cfg, get_report_request,
				      sizeof(get_report_request));
	callbacks->buf_read_requested(target_cfg, &read_ptr, &read_len);
	callbacks->stop(target_cfg);
	zassert_equal(*(uint16_t *)read_ptr, 3);
	zassert_equal(read_ptr[2], REPORT_ID_DEVICE_CAPS);

	get_report_request[2] = 99;
	callbacks->buf_write_received(target_cfg, get_report_request,
				      sizeof(get_report_request));
	callbacks->buf_read_requested(target_cfg, &read_ptr, &read_len);
	callbacks->stop(target_cfg);
	zassert_equal(*(uint16_t *)read_ptr, 0);
}

static void hid_i2c_touchpad_before(void *fixture)
{
	const struct gpio_dt_spec *hid_irq =
		GPIO_DT_FROM_NODELABEL(gpio_ec_ap_hid_int_odl);

	gpio_pin_set_dt(hid_irq, 1);
}

ZTEST_SUITE(hid_i2c_touchpad, drivers_predicate_post_main, NULL,
	    hid_i2c_touchpad_before, NULL, NULL);
