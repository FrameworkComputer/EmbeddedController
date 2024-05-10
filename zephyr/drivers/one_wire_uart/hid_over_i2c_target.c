/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_hid_i2c_touchpad

#include "drivers/one_wire_uart.h"
#include "drivers/one_wire_uart_internal.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "usb_hid_touchpad.h"

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define HID_DESC_REG 0x01
#define REPORT_DESC_REG 0x02
#define INPUT_REG 0x03
#define OUTPUT_REG 0x04
#define CMD_REG 0x05
#define DATA_REG 0x06

#define USB_UPDATER_WRITE_REG 0x10
#define USB_UPDATER_READ_REG 0x11

#define HID_DESC_LENGTH 30

#define OP_CODE_RESET 1
#define OP_CODE_GET_REPORT 2

const static struct device *one_wire_uart =
	DEVICE_DT_GET(DT_NODELABEL(one_wire_uart));

static void hid_reset(const struct device *dev)
{
	const struct i2c_target_dev_config *cfg = dev->config;
	struct i2c_target_data *data = dev->data;

	k_msgq_purge(data->touchpad_report_queue);
	gpio_pin_set_dt(&cfg->irq, 1);
	data->in_reset = true;
}

static int hid_get_report(int report_id, uint8_t *out)
{
	if (report_id == REPORT_ID_DEVICE_CERT) {
		*(uint16_t *)out = 257;
		out[2] = REPORT_ID_DEVICE_CERT;
		/* TODO:
		 * Fill the buf with 256 zeroes looks fine for linux kernel,
		 * check if we really need the device cert.
		 */
		memset(out + 3, 0, 256);
	} else if (report_id == REPORT_ID_DEVICE_CAPS) {
		*(uint16_t *)out = 3;
		out[2] = REPORT_ID_DEVICE_CAPS;
		out[3] = MAX_FINGERS;
		out[4] = 0;
	} else {
		*(uint16_t *)out = 0;
	}

	return *(uint16_t *)out;
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
static int hid_handler(const struct device *dev, const uint8_t *in, int in_size,
		       uint8_t *out)
{
	const struct i2c_target_dev_config *cfg = dev->config;
	struct i2c_target_data *data = dev->data;

	if (in_size == 0) { /* read report */
		if (!data->in_reset) {
			int ret;

			ret = k_msgq_get(data->touchpad_report_queue, out + 2,
					 K_NO_WAIT);
			if (ret == 0) {
				*(uint16_t *)out =
					sizeof(struct usb_hid_touchpad_report);
			}

			if (k_msgq_num_used_get(data->touchpad_report_queue) ==
			    0) {
				gpio_pin_set_dt(&cfg->irq, 0);
			}

			return (ret ? 0 :
				      sizeof(struct usb_hid_touchpad_report)) +
			       2;
		} else {
			/* first report after reset is always [0x00, 0x00] */
			out[0] = 0;
			out[1] = 0;
			data->in_reset = false;
			gpio_pin_set_dt(&cfg->irq, 0);
			k_msgq_purge(data->touchpad_report_queue);
			return 2;
		}
	}

	int reg = in[0];

	if (reg == HID_DESC_REG) {
		memcpy(out, cfg->hid_desc, HID_DESC_LENGTH);
		return HID_DESC_LENGTH;
	}

	if (reg == REPORT_DESC_REG) {
		memcpy(out, cfg->report_desc, cfg->report_desc_length);
		return cfg->report_desc_length;
	}

	if (reg == CMD_REG) {
		if (in_size < 4) {
			return 0;
		}

		int cmd = in[2] | (in[3] << 8);
		int op_code = (cmd >> 8) & 0xF;

		switch (op_code) {
		case OP_CODE_RESET:
			hid_reset(dev);
			break;
		case OP_CODE_GET_REPORT:
			return hid_get_report(cmd & 0xF, out);
		default:
			break;
		}

		return 0;
	}

	if (reg == USB_UPDATER_WRITE_REG) {
		one_wire_uart_send(one_wire_uart, ROACH_CMD_UPDATER_COMMAND,
				   in + 1, in_size - 1);
		return 0;
	}

	if (reg == USB_UPDATER_READ_REG) {
		out[0] = ring_buf_get(data->usb_update_queue, out + 1, 255);
		return out[0] + 1;
	}

	return 0;
}

static int hid_i2c_target_stop(struct i2c_target_config *config)
{
	struct i2c_target_data *data =
		CONTAINER_OF(config, struct i2c_target_data, config);

	if (data->write_buf_len) {
		hid_handler(data->dev, data->write_buf, data->write_buf_len,
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
	int ret = hid_handler(data->dev, data->write_buf, data->write_buf_len,
			      data->read_buf);

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

static int hid_i2c_target_init(const struct device *dev)
{
	const struct i2c_target_dev_config *cfg = dev->config;

	/* Check I2C controller ready. */
	if (!device_is_ready(cfg->bus.bus)) {
		return -ENODEV;
	}

	return 0;
}

void hid_i2c_touchpad_add(const struct device *dev,
			  const struct usb_hid_touchpad_report *report)
{
	const struct i2c_target_dev_config *cfg = dev->config;
	struct i2c_target_data *data = dev->data;

	k_msgq_put(data->touchpad_report_queue, report, K_NO_WAIT);
	gpio_pin_set_dt(&cfg->irq, 1);
}

#define HID_I2C_TARGET_INIT(inst)                                              \
	K_MSGQ_DEFINE(touchpad_report_queue##inst,                             \
		      sizeof(struct usb_hid_touchpad_report), 16, 1);          \
	RING_BUF_DECLARE(usb_update_queue##inst, 256);                         \
	static const uint8_t report_desc##inst[] =                             \
		REPORT_DESC(DT_INST_PROP(inst, max_pressure),                  \
			    DT_INST_PROP(inst, logical_max_x),                 \
			    DT_INST_PROP(inst, logical_max_y),                 \
			    DT_INST_PROP(inst, physical_max_x),                \
			    DT_INST_PROP(inst, physical_max_y));               \
	static const uint16_t hid_desc##inst[] = {                             \
		0x1E, /* HIDDescLength */                                      \
		0x100, /* bcdVersion 0x100 */                                  \
		sizeof(report_desc##inst), /* ReportDescLength */              \
		REPORT_DESC_REG, /* ReportDescRegister */                      \
		INPUT_REG, /* InputRegister */                                 \
		sizeof(struct usb_hid_touchpad_report), /* MaxInputLength */   \
		OUTPUT_REG, /* OutputRegister (unused) */                      \
		0, /* MaxOutputLength */                                       \
		CMD_REG, /* CommandRegister */                                 \
		DATA_REG, /* DataRegister */                                   \
		DT_INST_PROP(inst, vid), /* VID */                             \
		DT_INST_PROP(inst, pid), /* PID */                             \
		0x1, /* VersionID */                                           \
		0, /* Reserved */                                              \
		0, /* Reserved */                                              \
	};                                                                     \
	static const struct i2c_target_dev_config i2c_target_cfg##inst = {     \
		.bus = I2C_DT_SPEC_GET(DT_DRV_INST(inst)),                     \
		.irq = GPIO_DT_SPEC_GET(DT_DRV_INST(inst), irq_gpios),         \
		.report_desc = report_desc##inst,                              \
		.report_desc_length = sizeof(report_desc##inst),               \
		.hid_desc = hid_desc##inst,                                    \
	};                                                                     \
	static struct i2c_target_data i2c_target_data ## inst = {            \
		.config = {                                                  \
			.address = DT_INST_REG_ADDR(inst),                   \
			.callbacks = &target_callbacks,                      \
		},                                                           \
		.dev = DEVICE_DT_INST_GET(inst),                             \
		.in_reset = true,                                            \
		.touchpad_report_queue = &touchpad_report_queue ## inst,     \
		.usb_update_queue = &usb_update_queue ## inst,               \
	}; \
	I2C_DEVICE_DT_INST_DEFINE(inst, hid_i2c_target_init, NULL,             \
				  &i2c_target_data##inst,                      \
				  &i2c_target_cfg##inst, POST_KERNEL,          \
				  CONFIG_I2C_TARGET_INIT_PRIORITY, &api_funcs)

DT_INST_FOREACH_STATUS_OKAY(HID_I2C_TARGET_INIT);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
