/*
 * Copyright (c) 2023 Framework Computer
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#include "hid_device.h"

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/util.h>



#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>


#include "console.h"
#include "zephyr_console_shim.h"
#include "i2c_hid.h"
#include "hooks.h"
#include "host_command.h"
#include "util.h"
#include "math_util.h"


#ifdef CONFIG_BOARD_LOTUS
#include "touchpad_descriptor.h"
#endif

#define DT_DRV_COMPAT cros_ec_i2c_target_hid

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL

LOG_MODULE_REGISTER(hid_target);

/* Framework HID fields */
#define I2C_HID_MEDIAKEYS_VENDOR_ID 0x32AC
#define I2C_HID_MEDIAKEYS_PRODUCT_ID 0x0006
#define I2C_HID_ALS_PRODUCT_ID 0x001B

#define I2C_HID_MEDIAKEYS_FW_VERSION 0x0001
#define I2C_HID_MEDIAKEYS_HID_DESC_REGISTER	0x0055

#define I2C_HID_HEADER_SIZE 3

#define REPORT_ID_RADIO		0x01
#define REPORT_ID_CONSUMER	0x02
#define REPORT_ID_SENSOR	0x03

#define ALS_REPORT_STOP		0x00
#define ALS_REPORT_POLLING	0x01
#define ALS_REPORT_THRES	0x02



#define EVENT_HID_HOST_IRQ	0x8000
#define EVENT_REPORT_ILLUMINANCE_VALUE	0x4000

/**
 * ALS HID Unit Exponent
 * 0x00 = 1	(Default)
 * 0x0C = 0.0001
 * 0x0D = 0.001
 * 0x0E = 0.01
 * 0x0F = 0.1
 */
#define ALS_HID_UNIT	0x00

#define HID_ALS_MAX 10000
#define HID_ALS_MIN 0
/* Note sensitivity is scaled by exponent 0.01*/
#define HID_ALS_SENSITIVITY 100

#define HID_ALS_REPORT_INTERVAL 1000

/* HID_USAGE_SENSOR_PROPERTY_SENSOR_CONNECTION_TYPE */
#define HID_INTEGRATED			1
#define HID_ATTACHED			2
#define HID_EXTERNAL			3

/* HID_USAGE_SENSOR_PROPERTY_REPORTING_STATE */
#define HID_NO_EVENTS			1
#define HID_ALL_EVENTS			2
#define HID_THRESHOLD_EVENTS		3
#define HID_NO_EVENTS_WAKE		4
#define HID_ALL_EVENTS_WAKE		5
#define HID_THRESHOLD_EVENTS_WAKE	6

/* HID_USAGE_SENSOR_PROPERTY_POWER_STATE */
#define HID_UNDEFINED			1
#define HID_D0_FULL_POWER		2
#define HID_D1_LOW_POWER		3
#define HID_D2_STANDBY_WITH_WAKE	4
#define HID_D3_SLEEP_WITH_WAKE		5
#define HID_D4_POWER_OFF		6

/* HID_USAGE_SENSOR_STATE */
#define HID_UNKNOWN			1
#define HID_READY			2
#define HID_NOT_AVAILABLE		3
#define HID_NO_DATA			4
#define HID_INITIALIZING		5
#define HID_ACCESS_DENIED		6
#define HID_ERROR			7

/* HID_USAGE_SENSOR_EVENT */
#define HID_UNKNOWN			1
#define HID_STATE_CHANGED		2
#define HID_PROPERTY_CHANGED		3
#define HID_DATA_UPDATED		4
#define HID_POLL_RESPONSE		5
#define HID_CHANGE_SENSITIVITY		6


struct radio_report {
	uint8_t state;
} __packed;

struct consumer_button_report {
	uint16_t button_id;
} __packed;

struct als_input_report {
	uint8_t sensor_state;
	uint8_t event_type;
	uint16_t illuminanceValue;
} __packed;

struct als_feature_report {
	/* Common properties */
	uint8_t connection_type;
	uint8_t reporting_state;
	uint8_t power_state;
	uint8_t sensor_state;
	uint32_t report_interval;

	/* Properties specific to this sensor */
	uint16_t sensitivity;
	uint16_t maximum;
	uint16_t minimum;
} __packed;


#ifndef CONFIG_BOARD_LOTUS

static struct radio_report radio_button;
static struct consumer_button_report consumer_button;
/* HID input report descriptor
 *
 * For a complete reference, please see the following docs on usb.org
 *
 * 1. Device Class Definition for HID
 * 2. HID Usage Tables
 */

static const uint8_t keyboard_report_desc[] = {
	/* Airplane Radio Collection */
	0x05, 0x01,		/* USAGE_PAGE (Generic Desktop) */
	0x09, 0x0C,		/* USAGE (Wireless Radio Controls) */
	0xA1, 0x01,		/* COLLECTION (Application) */
	0x85, REPORT_ID_RADIO,	/* Report ID (Radio) */
	0x15, 0x00,		/* LOGICAL_MINIMUM (0) */
	0x25, 0x01,		/* LOGICAL_MAXIMUM (1) */
	0x09, 0xC6,		/* USAGE (Wireless Radio Button) */
	0x95, 0x01,		/* REPORT_COUNT (1) */
	0x75, 0x01,		/* REPORT_SIZE (1) */
	0x81, 0x06,		/* INPUT (Data,Var,Rel) */
	0x75, 0x07,		/* REPORT_SIZE (7) */
	0x81, 0x03,		/* INPUT (Cnst,Var,Abs) */
	0xC0,			/* END_COLLECTION */

	/* Consumer controls collection */
	0x05, 0x0C,		/* USAGE_PAGE (Consumer Devices) */
	0x09, 0x01,		/* USAGE (Consumer Control) */
	0xA1, 0x01,		/* COLLECTION (Application) */
	0x85, REPORT_ID_CONSUMER,	/* Report ID (Consumer) */
	0x15, 0x00,		/* LOGICAL_MINIMUM (0x0) */
	0x26, 0xFF, 0x03,	/* LOGICAL_MAXIMUM (0x3FF) */
	0x19, 0x00,		/* Usage Minimum (0) */
	0x2A, 0xFF, 0x03,	/* Usage Maximum (0) */
	0x75, 0x10,		/* Report Size (16) */
	0x95, 0x01,		/* Report Count (1) */
	0x81, 0x00,		/* Input (Data,Arr,Abs) */
	0xC0,			/* END_COLLECTION */
};

static struct i2c_hid_descriptor keyboard_hid_desc = {
	.wHIDDescLength = I2C_HID_DESC_LENGTH,
	.bcdVersion = I2C_HID_BCD_VERSION,
	.wReportDescLength = sizeof(keyboard_report_desc),
	.wReportDescRegister = I2C_HID_REPORT_DESC_REGISTER,
	.wInputRegister = I2C_HID_INPUT_REPORT_REGISTER,
	/*Note if there are multiple reports wMaxInputLength has to be max*/
	.wMaxInputLength = I2C_HID_HEADER_SIZE +
			   sizeof(struct consumer_button_report),
	.wOutputRegister = 0,
	.wMaxOutputLength = 0,
	.wCommandRegister = I2C_HID_COMMAND_REGISTER,
	.wDataRegister = I2C_HID_DATA_REGISTER,
	.wVendorID = I2C_HID_MEDIAKEYS_VENDOR_ID,
	.wProductID = I2C_HID_MEDIAKEYS_PRODUCT_ID,
	.wVersionID = I2C_HID_MEDIAKEYS_FW_VERSION,
};

#endif


static struct als_input_report als_sensor;
static struct als_feature_report als_feature;

static const uint8_t als_report_desc[] = {

	/* Sensor controls collection */
	0x05, 0x20,		/* USAGE_PAGE (sensor) */
	0x09, 0x41,		/* USAGE ID (Light: Ambient Light) */
	0xA1, 0x00,		/* COLLECTION (Physical) */
	0x85, REPORT_ID_SENSOR,	/* Report ID (Sensor) */

	0x05, 0x20,		/* USAGE PAGE (Sensor) */
	0x0A, 0x09, 0x03,	/* USAGE ID (Property: Sensor Connection Type) */
	0x15, 0x00,		/* LOGICAL_MINIMUN (0x00) */
	0x25, 0x02,		/* LOGICAL_MAXIMUM (0x02) */
	0x75, 0x08,		/* Report Size (8) */
	0x95, 0x01,		/* Report Count (1) */
	0xA1, 0x02,		/* COLLECTION (logical) */
	0x0A, 0x30, 0x08,	/* Connection Type: PC Integrated */
	0x0A, 0x31, 0x08,	/* Connection Type: PC Attached */
	0x0A, 0x32, 0x08,	/* Connection Type: PC External */
	0xB1, 0x00,		/* Feature (Data,Arr,Abs) */
	0xC0,			/* END_COLLECTION */

	0x0A, 0x16, 0x03,	/* USAGE ID (Property: Reporting State) */
	0x15, 0x00,		/* LOGICAL_MINIMUN (0x00) */
	0x25, 0x05,		/* LOGICAL_MAXIMUM (0x05) */
	0x75, 0x08,		/* Report Size (8) */
	0x95, 0x01,		/* Report Count (1) */
	0xA1, 0x02,		/* COLLECTION (logical) */
	0x0A, 0x40, 0x08,	/* Reporting State: Report No Events */
	0x0A, 0x41, 0x08,	/* Reporting State: Report All Events */
	0x0A, 0x42, 0x08,	/* Reporting State: Report Threshold Events */
	0x0A, 0x43, 0x08,	/* Reporting State: Wake On No Events */
	0x0A, 0x44, 0x08,	/* Reporting State: Wake On All Events */
	0x0A, 0x45, 0x08,	/* Reporting State: Wake On Threshold Events */
	0xB1, 0x00,		/* Feature (Data,Arr,Abs) */
	0xC0,			/* END_COLLECTION */

	0x0A, 0x19, 0x03,	/* USAGE ID (Property: Power State Undefined Select) */
	0x15, 0x00,		/* LOGICAL_MINIMUN (0x00) */
	0x25, 0x05,		/* LOGICAL_MAXIMUM (0x05) */
	0x75, 0x08,		/* Report Size (8) */
	0x95, 0x01,		/* Report Count (1) */
	0xA1, 0x02,		/* COLLECTION (logical) */
	0x0A, 0x50, 0x08,	/* Power State: Undefined */
	0x0A, 0x51, 0x08,	/* Power State: D0 Full Power */
	0x0A, 0x52, 0x08,	/* Power State: D1 Low Power */
	0x0A, 0x53, 0x08,	/* Power State: D2 Standby Power with Wakeup */
	0x0A, 0x54, 0x08,	/* Power State: D3 sleep with Wakeup */
	0x0A, 0x55, 0x08,	/* Power State: D4 Power Off */
	0xB1, 0x00,		/* Feature (Data,Arr,Abs) */
	0xC0,			/* END_COLLECTION */

	0x0A, 0x01, 0x02,	/* USAGE ID (Event: Sensor State) */
	0x15, 0x00,		/* LOGICAL_MINIMUN (0x00) */
	0x25, 0x06,		/* LOGICAL_MAXIMUM (0x06) */
	0x75, 0x08,		/* Report Size (8) */
	0x95, 0x01,		/* Report Count (1) */
	0xA1, 0x02,		/* COLLECTION (logical) */
	0x0A, 0x00, 0x08,	/* Sensor State: Undefined */
	0x0A, 0x01, 0x08,	/* Sensor State: Ready */
	0x0A, 0x02, 0x08,	/* Sensor State: Not Available */
	0x0A, 0x03, 0x08,	/* Sensor State: No Data */
	0x0A, 0x04, 0x08,	/* Sensor State: Initializing */
	0x0A, 0x05, 0x08,	/* Sensor State: Access Denied */
	0x0A, 0x06, 0x08,	/* Sensor State: Error */
	0xB1, 0x00,		/* Feature (Data,Arr,Abs) */
	0xC0,			/* END_COLLECTION */

	0x0A, 0x0E, 0x03,	/* USAGE ID (Property: Report Interval) */
	0x15, 0x00,		/* LOGICAL_MINIMUN (0x00) */
	0x27, 0xFF, 0xFF, 0xFF, 0XFF,	/* LOGICAL_MAXIMUM (0xFFFFFFFF) */
	0x75, 0x20,		/* Report Size (32) */
	0x95, 0x01,		/* Report Count (1) */
	0x55, 0x00,		/* UNIT EXPONENT (0x00) */
	0xB1, 0x02,		/* Feature (Data,Var,Abs) */

	0x0A, 0xD1, 0xE4,	/* USAGE ID (Modified Change Sensitivity Percent of Range) */
	0x15, 0x00,		/* LOGICAL_MINIMUN (0x00) */
	0x26, 0x10, 0x27,	/* LOGICAL_MAXIMUM (0x2710) */
	0x75, 0x10,		/* Report Size (16) */
	0x95, 0x01,		/* Report Count (1) */
	0x55, 0x0E,		/* UNIT EXPONENT (0x0E 0.01) */
	0xB1, 0x02,		/* Feature (Data,Var,Abs) */

	0x0A, 0xD1, 0x24,	/* USAGE ID (Modified Maximum) */
	0x15, 0x00,		/* LOGICAL_MINIMUN (0x00) */
	0x26, 0xFF, 0xFF,	/* LOGICAL_MAXIMUM (0xFFFF) */
	0x75, 0x10,		/* Report Size (16) */
	0x95, 0x01,		/* Report Count (1) */
	0x55, 0x00,		/* UNIT EXPONENT (0x00) */
	0xB1, 0x02,		/* Feature (Data,Var,Abs) */

	0x0A, 0xD1, 0x34,	/* USAGE ID (Modified Minimum) */
	0x15, 0x00,		/* LOGICAL_MINIMUN (0x00) */
	0x26, 0xFF, 0xFF,	/* LOGICAL_MAXIMUM (0xFFFF) */
	0x75, 0x10,		/* Report Size (16) */
	0x95, 0x01,		/* Report Count (1) */
	0x55, 0x00,		/* UNIT EXPONENT (0x00) */
	0xB1, 0x02,		/* Feature (Data,Var,Abs) */

	0x05, 0x20,		/* USAGE PAGE (Sensor) */
	0x0A, 0x01, 0x02,	/* USAGE ID (Event: Sensor State) */
	0x15, 0x00,		/* LOGICAL_MINIMUN (0x00) */
	0x25, 0x06,		/* LOGICAL_MAXIMUM (0x06) */
	0x75, 0x08,		/* Report Size (8) */
	0x95, 0x01,		/* Report Count (1) */
	0xA1, 0x02,		/* COLLECTION (logical) */
	0x0A, 0x00, 0x08,	/* Sensor State: Undefined */
	0x0A, 0x01, 0x08,	/* Sensor State: Ready */
	0x0A, 0x02, 0x08,	/* Sensor State: Not Available */
	0x0A, 0x03, 0x08,	/* Sensor State: No Data */
	0x0A, 0x04, 0x08,	/* Sensor State: Initializing */
	0x0A, 0x05, 0x08,	/* Sensor State: Access Denied */
	0x0A, 0x06, 0x08,	/* Sensor State: Error */
	0x81, 0x00,		/* Input (Data,Arr,Abs) */
	0xC0,			/* END_COLLECTION */

	0x0A, 0x02, 0x02,	/* USAGE (Sensor event) */
	0x15, 0x00,		/* LOGICAL_MINIMUN (0x00) */
	0x25, 0x05,		/* LOGICAL_MAXIMUM (0x05) */
	0x75, 0x08,		/* Report Size (8) */
	0x95, 0x01,		/* Report Count (1) */
	0xA1, 0x02,		/* COLLECTION (logical) */
	0x0A, 0x10, 0x08,	/* Sensor Event: Unknown */
	0x0A, 0x11, 0x08,	/* Sensor Event: State Changed */
	0x0A, 0x12, 0x08,	/* Sensor Event: Property Changed */
	0x0A, 0x13, 0x08,	/* Sensor Event: Data Updated */
	0x0A, 0x14, 0x08,	/* Sensor Event: Poll Response */
	0x0A, 0x15, 0x08,	/* Sensor Event: Change Sensitivity */
	0x81, 0x00,		/* Input (Data,Arr,Abs) */
	0xC0,			/* END_COLLECTION */

	0x0A, 0xD1, 0x04,	/* USAGE (Data Field: Illuminance) */
	0x15, 0x00,		/* LOGICAL_MINIMUN (0x00) */
	0x26, 0xFF, 0xFF,	/* LOGICAL_MAXIMUM (0XFFFF) */
	0x55, ALS_HID_UNIT,	/* UNIT EXPONENT (0x00) */
	0x75, 0x10,		/* Report Size (16) */
	0x95, 0x01,		/* Report Count (1) */
	0x81, 0x02,		/* Input (Data,Arr,Abs) */
	0xC0,			/* END_COLLECTION */
};




static struct i2c_hid_descriptor als_hid_desc = {
	.wHIDDescLength = I2C_HID_DESC_LENGTH,
	.bcdVersion = I2C_HID_BCD_VERSION,
	.wReportDescLength = sizeof(als_report_desc),
	.wReportDescRegister = I2C_HID_REPORT_DESC_REGISTER,
	.wInputRegister = I2C_HID_INPUT_REPORT_REGISTER,
	/*Note if there are multiple reports wMaxInputLength has to be max*/
	.wMaxInputLength = I2C_HID_HEADER_SIZE +
			   sizeof(struct als_input_report),
	.wOutputRegister = 0,
	.wMaxOutputLength = 0,
	.wCommandRegister = I2C_HID_COMMAND_REGISTER,
	.wDataRegister = I2C_HID_DATA_REGISTER,
	.wVendorID = I2C_HID_MEDIAKEYS_VENDOR_ID,
	.wProductID = I2C_HID_ALS_PRODUCT_ID,
	.wVersionID = I2C_HID_MEDIAKEYS_FW_VERSION,
};

struct i2c_hid_target_data {
	struct i2c_target_config config;
	uint32_t buffer_size;
	uint8_t *buffer;
	uint32_t buffer_idx;
	size_t write_idx;
	uint16_t target_register;
	uint8_t command;
	uint8_t power_state;
	uint8_t report_id;
	uint8_t report_type;
	const struct gpio_dt_spec *alert_gpio;
	const uint8_t *report_descriptor;
	size_t report_descriptor_size;
	const struct i2c_hid_descriptor *descriptor;
	size_t descriptor_size;
};

struct i2c_hid_target_config {
	struct i2c_dt_spec bus;
	uint32_t buffer_size;
	uint8_t *buffer;
	struct gpio_dt_spec alert_gpio;
	const uint8_t *report_descriptor;
	size_t report_descriptor_size;
	const struct i2c_hid_descriptor *descriptor;
	size_t descriptor_size;


};
#ifndef CONFIG_BOARD_LOTUS
void irq_keyboard(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(i2chid0));
	struct i2c_hid_target_data *data = dev->data;

	gpio_pin_set_dt(data->alert_gpio, 0);
}

void hid_consumer(uint16_t  id, bool pressed)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(i2chid0));
	struct i2c_hid_target_data *data = dev->data;

	data->report_id = REPORT_ID_CONSUMER;
	if (pressed) {
		consumer_button.button_id = id;
	} else {
		consumer_button.button_id = 0;
	}

	irq_keyboard();
}
void hid_airplane(bool pressed)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(i2chid0));
	struct i2c_hid_target_data *data = dev->data;

	data->report_id = REPORT_ID_RADIO;
	radio_button.state = pressed;
	if (pressed)
		irq_keyboard();
}
#endif


static void hid_target_als_irq(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(i2chid1));
	struct i2c_hid_target_data *data = dev->data;

	data->report_id = REPORT_ID_SENSOR;
	gpio_pin_set_dt(data->alert_gpio, 0);
}

void i2c_hid_als_init(void)
{
	als_feature.connection_type = HID_INTEGRATED;
	als_feature.reporting_state = HID_NO_EVENTS;
	als_feature.power_state = HID_D0_FULL_POWER;
	als_feature.sensor_state = HID_READY;
	als_feature.report_interval = HID_ALS_REPORT_INTERVAL;
	als_feature.sensitivity = HID_ALS_SENSITIVITY;
	als_feature.maximum = HID_ALS_MAX;
	als_feature.minimum = HID_ALS_MIN;

	als_sensor.event_type = HID_DATA_UPDATED;
	als_sensor.sensor_state = HID_READY;
	als_sensor.illuminanceValue = 0x0000;
}
DECLARE_HOOK(HOOK_INIT, i2c_hid_als_init, HOOK_PRIO_DEFAULT);

void report_illuminance_value(void);
DECLARE_DEFERRED(report_illuminance_value);
void report_illuminance_value(void)
{
	uint16_t newIlluminaceValue = *(uint16_t *)host_get_memmap(EC_MEMMAP_ALS);
	int granularity = als_feature.sensitivity * als_sensor.illuminanceValue / 10000;
	uint32_t report_interval = als_feature.report_interval;
	switch (als_feature.reporting_state) {
	/* we did not implement threshold reporting in our HID descriptor */
	case HID_THRESHOLD_EVENTS:
	case HID_THRESHOLD_EVENTS_WAKE:
	case HID_ALL_EVENTS:
	case HID_ALL_EVENTS_WAKE:
		if (ABS(als_sensor.illuminanceValue - newIlluminaceValue) > granularity) {
			als_sensor.illuminanceValue = newIlluminaceValue;
			als_sensor.event_type = HID_DATA_UPDATED;
			hid_target_als_irq();
		}
		break;
	default:
		break;
	}

	if (report_interval == 0) {
		/* per hid spec report interval should be sensor default when 0 */
		report_interval = HID_ALS_REPORT_INTERVAL;
	}
	hook_call_deferred(&report_illuminance_value_data,
		report_interval * MSEC);

}


static void als_report_control(uint8_t report_mode)
{
	if (report_mode) {
		hook_call_deferred(&report_illuminance_value_data,
			((int) als_feature.report_interval) * MSEC);
	} else {
		/* stop report als value */
		hook_call_deferred(&report_illuminance_value_data, -1);
	}
}



static void als_shutdown(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(i2chid1));
	struct i2c_hid_target_data *data = dev->data;

	als_feature.power_state = HID_D4_POWER_OFF;

	als_report_control(ALS_REPORT_STOP);

	gpio_pin_set_dt(data->alert_gpio, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, als_shutdown, HOOK_PRIO_DEFAULT);

static void extract_report(size_t len, const uint8_t *buffer, void *data,
			   size_t data_len)
{
	if (len != 7 + data_len) {
		return;
	}
	memcpy(data, buffer + 7, data_len);
}

static size_t fill_report(uint8_t *buffer, uint8_t report_id, const void *data,
			  size_t data_len)
{
	size_t response_len = I2C_HID_HEADER_SIZE + data_len;

	buffer[0] = response_len & 0xFF;
	buffer[1] = (response_len >> 8) & 0xFF;
	buffer[2] = report_id;
	memcpy(buffer + I2C_HID_HEADER_SIZE, data, data_len);
	return response_len;
}

static int hid_target_write_requested(struct i2c_target_config *config)
{
	struct i2c_hid_target_data *data = CONTAINER_OF(config,
						struct i2c_hid_target_data,
						config);

	data->write_idx = 0;
	data->target_register = 0;
	data->buffer_idx = 0;

	return 0;
}

static int hid_target_write_received(struct i2c_target_config *config,
				       uint8_t val)
{
	struct i2c_hid_target_data *data = CONTAINER_OF(config,
						struct i2c_hid_target_data,
						config);
	int ret = 0;

	/* In case hid wants to be R/O, return !0 here could trigger
	 * a NACK to the I2C controller, support depends on the
	 * I2C controller support
	 */

	if (data->write_idx < 2) {
		data->target_register |= val << (data->write_idx * 8);
		data->write_idx++;
	} else {
		if (data->buffer_idx < data->buffer_size)
			data->buffer[data->buffer_idx++] = val;
		else {
			ret = -ENOBUFS;
		}
	}

	return ret;
}

static int hid_target_process_write(struct i2c_target_config *config)
{
	struct i2c_hid_target_data *data = CONTAINER_OF(config,
						struct i2c_hid_target_data,
						config);

	uint8_t command = data->buffer[1] & 0x0F;
	uint8_t power_state = data->buffer[0] & 0x03;
	uint8_t report_id = data->buffer[0] & 0x0F;
	uint8_t report_type = (data->buffer[0] & 0x30) >> 4;
	size_t response_size = 0;

	switch (command) {
	case I2C_HID_CMD_RESET:
		data->report_id = 0;
		gpio_pin_set_dt(data->alert_gpio, 0);
		break;
	case I2C_HID_CMD_GET_REPORT:
		data->report_id = report_id;
#ifdef CONFIG_BOARD_LOTUS
		if (data->descriptor->wCommandRegister == I2C_TOUCHPAD_HID_COMMAND_REGISTER) {
			switch (report_id) {
			case 2:
				static const uint8_t touchpad_feature_2[] = {0x04, 0x00, 0x02, 0x05, 0x00, 0x00, 0x00, 0x00};
				memcpy(data->buffer, touchpad_feature_2, sizeof(touchpad_feature_2));
				break;
			default:
				memset(data->buffer, 0, data->buffer_size);
				break;
			}
		} else
#endif /* CONFIG_BOARD_LOTUS */
		{
			switch (report_id) {
	#ifndef CONFIG_BOARD_LOTUS
			case REPORT_ID_RADIO:
					response_size = fill_report(data->buffer, report_id,
							&radio_button,
							sizeof(struct radio_report));
				break;
			case REPORT_ID_CONSUMER:
					response_size = fill_report(data->buffer, report_id,
							&consumer_button,
							sizeof(struct consumer_button_report));
				break;
	#endif
			case REPORT_ID_SENSOR:
				if (report_type == 0x01) {
					als_sensor.event_type = HID_POLL_RESPONSE;
					response_size = fill_report(data->buffer, report_id,
						&als_sensor,
						sizeof(struct als_input_report));
				} else if (report_type == 0x03) {
					response_size = fill_report(data->buffer, report_id,
						&als_feature,
						sizeof(struct als_feature_report));
				}
				break;
			default:
				response_size = fill_report(data->buffer, 0,
					NULL,
					0);
				break;
			}
		}
		break;
	case I2C_HID_CMD_SET_REPORT:
		switch (report_id) {
		case REPORT_ID_SENSOR:
			extract_report(data->buffer_idx,
						data->buffer,
						&als_feature,
						sizeof(struct als_feature_report));
			if (als_feature.power_state == HID_D4_POWER_OFF) {
				als_shutdown();
			} else {
				als_report_control(ALS_REPORT_POLLING);
			}
			break;
		default:
			break;
		}
		break;
	case I2C_HID_CMD_SET_POWER:
		/*
		 * Return the power setting so the user can actually set the
		 * touch controller's power state in board level.
		 */
		*data->buffer = power_state;

		if (power_state == 0x00) {
			i2c_hid_als_init();
			als_report_control(ALS_REPORT_POLLING);
		} else
			als_shutdown();

		break;
	default:

	}
	return 0;
}


static int hid_target_read_processed(struct i2c_target_config *config,
				       uint8_t *val)
{
	struct i2c_hid_target_data *data = CONTAINER_OF(config,
						struct i2c_hid_target_data,
						config);
	uint16_t target_register = 0;
	int ret = 0;

	if (data->write_idx) {
		target_register = data->target_register;
	}

	switch (target_register) {
#ifdef CONFIG_BOARD_LOTUS
	case I2C_TOUCHPAD_HID_DESC_REGISTER:
#endif
	case I2C_HID_MEDIAKEYS_HID_DESC_REGISTER:
		if (data->buffer_idx < data->descriptor_size) {
			*val = ((uint8_t *)data->descriptor)[data->buffer_idx++];
		} else {
			ret = -ENOBUFS;
		}
		break;
#ifdef CONFIG_BOARD_LOTUS
	case I2C_TOUCHPAD_HID_REPORT_DESC_REGISTER:
#endif
	case I2C_HID_REPORT_DESC_REGISTER:
		if (data->buffer_idx < data->report_descriptor_size) {
			*val = data->report_descriptor[data->buffer_idx++];
		} else {
			ret = -ENOBUFS;
		}
		break;

	default:
		/* Other registers are populated in the write rx*/
		if (data->buffer_idx < data->buffer_size)
			*val = data->buffer[data->buffer_idx++];
		else {
			*val = 0;
		}
	}
	/* Increment will be done in the next read_processed callback
	 * In case of STOP, the byte won't be taken in account
	 */

	return ret;
}




/* Called on first read */
static int hid_target_read_requested(struct i2c_target_config *config,
				       uint8_t *val)
{
	struct i2c_hid_target_data *data = CONTAINER_OF(config,
						struct i2c_hid_target_data,
						config);
	uint16_t target_register;

	gpio_pin_set_dt(data->alert_gpio, 1);

	if (data->write_idx) {
		target_register = data->target_register;

	} else {
		target_register = I2C_HID_INPUT_REPORT_REGISTER;
	}

	if (target_register == I2C_HID_COMMAND_REGISTER
#ifdef CONFIG_BOARD_LOTUS
		|| target_register == I2C_TOUCHPAD_HID_COMMAND_REGISTER
#endif
		)
	{
		if (data->buffer_idx) {
			hid_target_process_write(config);
			data->buffer_idx = 0;
		}
	} else if (target_register == I2C_HID_INPUT_REPORT_REGISTER) {
		/* Common input report requests. */
		switch (data->report_id) {
#ifndef CONFIG_BOARD_LOTUS
		case REPORT_ID_RADIO:
			fill_report(data->buffer, REPORT_ID_RADIO,
					&radio_button,
					sizeof(struct radio_report));
			radio_button.state = 0;
			break;
		case REPORT_ID_CONSUMER:
			fill_report(data->buffer, REPORT_ID_CONSUMER,
					&consumer_button,
					sizeof(struct consumer_button_report));
			break;
#endif
		case REPORT_ID_SENSOR:
			fill_report(data->buffer, REPORT_ID_SENSOR,
					&als_sensor,
					sizeof(struct als_input_report));
			break;
		default:
			/* issue reset protocol */
			data->report_id = 0;
			memset(data->buffer, 0, data->descriptor->wMaxInputLength);
			break;
		}
	}

	return hid_target_read_processed(config, val);
}


static int cmd_hidals_status(int argc, const char **argv)
{
	int i;
	char *e;

	if (argc == 2) {
		i = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		als_sensor.illuminanceValue = i;
		als_sensor.event_type = HID_DATA_UPDATED;
		hid_target_als_irq();
	}

	ccprintf("ALS Feature\n");
	ccprintf(" report_state:%d\n", als_feature.reporting_state);
	ccprintf(" Power:%d\n", als_feature.power_state);
	ccprintf(" Sensor:%d\n", als_feature.sensor_state);
	ccprintf(" Interval:%dms\n", als_feature.report_interval);
	ccprintf(" sensitivity:%d\n", als_feature.sensitivity);
	ccprintf(" illuminance:%d\n", als_sensor.illuminanceValue);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(hidals, cmd_hidals_status, "[lux]",
			"Get als device status");

static int hid_target_stop(struct i2c_target_config *config)
{
	struct i2c_hid_target_data *data = CONTAINER_OF(config,
						struct i2c_hid_target_data,
						config);

	/* Clear the interrupt when we have processed the packet */
	gpio_pin_set_dt(data->alert_gpio, 1);

	if (data->target_register == I2C_HID_COMMAND_REGISTER
#ifdef CONFIG_BOARD_LOTUS
		|| data->target_register == I2C_TOUCHPAD_HID_COMMAND_REGISTER
#endif
	) {
		if (data->buffer_idx) {
			hid_target_process_write(config);
		}
	}

	data->write_idx = 0;
	data->target_register = 0;
	data->buffer_idx = 0;

	return 0;
}

int hid_target_register(const struct device *dev)
{
	const struct i2c_hid_target_config *cfg = dev->config;
	struct i2c_hid_target_data *data = dev->data;

	return i2c_target_register(cfg->bus.bus, &data->config);
}

int hid_target_unregister(const struct device *dev)
{
	const struct i2c_hid_target_config *cfg = dev->config;
	struct i2c_hid_target_data *data = dev->data;

	return i2c_target_unregister(cfg->bus.bus, &data->config);
}

static const struct i2c_target_driver_api api_funcs = {
	.driver_register = hid_target_register,
	.driver_unregister = hid_target_unregister,
};

static const struct i2c_target_callbacks hid_callbacks = {
	.write_requested = hid_target_write_requested,
	.read_requested = hid_target_read_requested,
	.write_received = hid_target_write_received,
	.read_processed = hid_target_read_processed,
	.stop = hid_target_stop,
};

static int i2c_hid_target_init(const struct device *dev)
{
	struct i2c_hid_target_data *data = dev->data;
	const struct i2c_hid_target_config *cfg = dev->config;

	if (!device_is_ready(cfg->bus.bus)) {
		return -ENODEV;
	}

	data->buffer_size = cfg->buffer_size;
	data->buffer = cfg->buffer;
	data->config.address = cfg->bus.addr;
	data->config.callbacks = &hid_callbacks;
	data->alert_gpio = &cfg->alert_gpio;
	data->report_descriptor = cfg->report_descriptor;
	data->report_descriptor_size = cfg->report_descriptor_size;
	data->descriptor = cfg->descriptor;
	data->descriptor_size = cfg->descriptor_size;
	data->report_id = 0;

	als_report_control(ALS_REPORT_STOP);
	return 0;
}


#define I2C_HID_INIT(inst)						\
	static struct i2c_hid_target_data				\
		i2c_hid_target_##inst##_dev_data;			\
									\
	static uint8_t							\
	i2c_hid_target_##inst##_buffer[(DT_INST_PROP(inst, max_report_size))];	\
									\
	static const struct i2c_hid_target_config			\
		i2c_hid_target_##inst##_cfg = {			\
		.bus = I2C_DT_SPEC_INST_GET(inst),			\
		.buffer_size = DT_INST_PROP(inst, max_report_size),		\
		.buffer = i2c_hid_target_##inst##_buffer,		\
		.alert_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, alert_gpios, {}),		\
		.report_descriptor = DT_INST_STRING_TOKEN(inst, hid_report_descriptor),		\
		.report_descriptor_size = sizeof(DT_INST_STRING_TOKEN(inst, hid_report_descriptor)),		\
		.descriptor = &DT_INST_STRING_TOKEN(inst, hid_descriptor),		\
		.descriptor_size = sizeof(DT_INST_STRING_TOKEN(inst, hid_descriptor))		\
	};								\
	static void i2c_hid_##inst##_init(void) \
	{	\
			i2c_target_driver_register(DEVICE_DT_INST_GET(inst));	\
	}	\
	DECLARE_HOOK(HOOK_INIT, i2c_hid_##inst##_init, HOOK_PRIO_DEFAULT);	\
									\
	DEVICE_DT_INST_DEFINE(inst,					\
			    &i2c_hid_target_init,			\
			    NULL,			\
			    &i2c_hid_target_##inst##_dev_data,	\
			    &i2c_hid_target_##inst##_cfg,		\
			    POST_KERNEL,				\
			    CONFIG_I2C_TARGET_INIT_PRIORITY,		\
			    &api_funcs);

DT_INST_FOREACH_STATUS_OKAY(I2C_HID_INIT)
