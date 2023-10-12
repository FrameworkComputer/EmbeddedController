/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/one_wire_uart.h"
#include "hooks.h"
#include "usb_hid_touchpad.h"

#include <string.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include <ap_power/ap_power.h>

#define TP_NODE DT_NODELABEL(hid_i2c_target)

K_MSGQ_DEFINE(touchpad_report_queue, sizeof(struct usb_hid_touchpad_report), 16,
	      1);

static const uint8_t REPORT_DESC_BLOB[] = REPORT_DESC(
	DT_PROP(TP_NODE, max_pressure), DT_PROP(TP_NODE, logical_max_x),
	DT_PROP(TP_NODE, logical_max_y), DT_PROP(TP_NODE, physical_max_x),
	DT_PROP(TP_NODE, physical_max_y));

#define HID_DESC_REG 0x01
#define REPORT_DESC_REG 0x02
#define INPUT_REG 0x03
#define OUTPUT_REG 0x04
#define CMD_REG 0x05
#define DATA_REG 0x06

/* TODO: allocate usb VID/PID */
const static uint16_t HID_DESC[] = {
	0x1E, /* HIDDescLength */
	0x100, /* bcdVersion 0x100 */
	sizeof(REPORT_DESC_BLOB), /* ReportDescLength */
	REPORT_DESC_REG, /* ReportDescRegister */
	INPUT_REG, /* InputRegister */
	sizeof(struct usb_hid_touchpad_report), /* MaxInputLength */
	OUTPUT_REG, /* OutputRegister (unused) */
	0, /* MaxOutputLength */
	CMD_REG, /* CommandRegister */
	DATA_REG, /* DataRegister */
	DT_PROP(TP_NODE, vid), /* VID */
	DT_PROP(TP_NODE, pid), /* PID */
	0x1, /* VersionID */
	0, /* Reserved */
	0, /* Reserved */
};

struct i2c_target_dev_config {
	/* I2C alternate configuration */
	struct i2c_dt_spec bus;
};

struct i2c_target_data {
	struct i2c_target_config config;
	uint8_t write_buf[256];
	uint8_t read_buf[CONFIG_I2C_TARGET_IT8XXX2_MAX_BUF_SIZE];
	int write_buf_len;
	struct ap_power_ev_callback cb;
};

static bool in_reset = true;

static void hid_reset(void)
{
	k_msgq_purge(&touchpad_report_queue);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(ec_ap_hid_int_odl), 0);
	in_reset = true;
}

/**
 * @brief Process an hid-over-i2c command.
 *
 * @param in Pointer to input buffer
 * @param in_size Size of input.
 * @param out Pointer to output buffer.
 *
 * @return Number of bytes written to output buffer.
 */
static int hid_handler(const uint8_t *in, int in_size, uint8_t *out)
{
	if (in_size == 0) { /* read report */
		if (!in_reset) {
			int ret;

			ret = k_msgq_get(&touchpad_report_queue, out + 2,
					 K_NO_WAIT);
			if (ret == 0) {
				*(uint16_t *)out =
					sizeof(struct usb_hid_touchpad_report);
			}

			if (k_msgq_num_used_get(&touchpad_report_queue) == 0) {
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(
							ec_ap_hid_int_odl),
						1);
			}

			return (ret ? 0 :
				      sizeof(struct usb_hid_touchpad_report)) +
			       2;
		} else {
			/* first report after reset is always [0x00, 0x00] */
			out[0] = 0;
			out[1] = 0;
			in_reset = false;
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(ec_ap_hid_int_odl), 1);
			k_msgq_purge(&touchpad_report_queue);
			return 2;
		}
	}

	int reg = in[0];

	if (reg == HID_DESC_REG) {
		memcpy(out, HID_DESC, sizeof(HID_DESC));
		return sizeof(HID_DESC);
	}

	if (reg == REPORT_DESC_REG) {
		memcpy(out, REPORT_DESC_BLOB, sizeof(REPORT_DESC_BLOB));
		return sizeof(REPORT_DESC_BLOB);
	}

	if (reg == CMD_REG) {
		if (in_size < 4) {
			return 0;
		}

		int cmd = in[2] | (in[3] << 8);
		int op_code = (cmd >> 8) & 0xF;

		if (op_code == 1) { /* RESET */
			hid_reset();
		}
		/* TODO: implement GET_REPORT and SET_REPORT */
		return 0;
	}

	return 0;
}

static int hid_i2c_target_stop(struct i2c_target_config *config)
{
	struct i2c_target_data *data =
		CONTAINER_OF(config, struct i2c_target_data, config);

	if (data->write_buf_len) {
		hid_handler(data->write_buf, data->write_buf_len,
			    data->read_buf);
	}
	data->write_buf_len = 0;
	return 0;
}

static void hid_i2c_target_buf_write_received(struct i2c_target_config *config,
					      uint8_t *ptr, uint32_t len)
{
	struct i2c_target_data *data =
		CONTAINER_OF(config, struct i2c_target_data, config);

	memcpy(data->write_buf, ptr, MIN(len, ONE_WIRE_UART_MAX_PAYLOAD_SIZE));
	data->write_buf_len = MIN(len, ONE_WIRE_UART_MAX_PAYLOAD_SIZE);
}

static int hid_i2c_target_buf_read_requested(struct i2c_target_config *config,
					     uint8_t **ptr, uint32_t *len)
{
	struct i2c_target_data *data =
		CONTAINER_OF(config, struct i2c_target_data, config);
	int ret = hid_handler(data->write_buf, data->write_buf_len,
			      data->read_buf);

	if (ret < 0) {
		return ret;
	}

	data->write_buf_len = 0;
	*ptr = data->read_buf;
	*len = ret;

	return 0;
}

static const struct i2c_target_callbacks target_callbacks = {
	.buf_write_received = hid_i2c_target_buf_write_received,
	.buf_read_requested = hid_i2c_target_buf_read_requested,
	.stop = hid_i2c_target_stop,
};

static int hid_i2c_target_register(const struct device *dev)
{
	const struct i2c_target_dev_config *cfg = dev->config;
	struct i2c_target_data *data = dev->data;

	return i2c_target_register(cfg->bus.bus, &data->config);
}

static int hid_i2c_target_unregister(const struct device *dev)
{
	const struct i2c_target_dev_config *cfg = dev->config;
	struct i2c_target_data *data = dev->data;

	return i2c_target_unregister(cfg->bus.bus, &data->config);
}

static const struct i2c_target_driver_api api_funcs = {
	.driver_register = hid_i2c_target_register,
	.driver_unregister = hid_i2c_target_unregister,
};

static void hid_i2c_suspend_handler(struct ap_power_ev_callback *cb,
				    struct ap_power_ev_data data)
{
	const struct device *hid_i2c_target =
		DEVICE_DT_GET(DT_NODELABEL(hid_i2c_target));

	switch (data.event) {
	default:
		return;

	case AP_POWER_RESUME:
		i2c_target_driver_register(hid_i2c_target);
		break;

	case AP_POWER_SUSPEND:
		i2c_target_driver_unregister(hid_i2c_target);
		break;
	}
}

static int hid_i2c_target_init(const struct device *dev)
{
	const struct i2c_target_dev_config *cfg = dev->config;
	struct i2c_target_data *data = dev->data;

	/* Check I2C controller ready. */
	if (!device_is_ready(cfg->bus.bus)) {
		return -ENODEV;
	}

	data->config.address = cfg->bus.addr;
	data->config.callbacks = &target_callbacks;

	ap_power_ev_init_callback(&data->cb, hid_i2c_suspend_handler,
				  AP_POWER_RESUME | AP_POWER_SUSPEND);
	ap_power_ev_add_callback(&data->cb);

	return 0;
}

void hid_i2c_touchpad_add(const struct usb_hid_touchpad_report *report)
{
	k_msgq_put(&touchpad_report_queue, report, K_NO_WAIT);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(ec_ap_hid_int_odl), 0);
}

static const struct i2c_target_dev_config i2c_target_cfg = {
	.bus = I2C_DT_SPEC_GET(DT_NODELABEL(hid_i2c_target)),
};

static struct i2c_target_data i2c_target_data;

I2C_DEVICE_DT_DEFINE(DT_NODELABEL(hid_i2c_target), hid_i2c_target_init, NULL,
		     &i2c_target_data, &i2c_target_cfg, POST_KERNEL,
		     CONFIG_I2C_TARGET_INIT_PRIORITY, &api_funcs);
