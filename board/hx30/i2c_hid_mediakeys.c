/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "config.h"
#include "console.h"
#include "task.h"
#include "hooks.h"
#include "i2c.h"
#include "util.h"
#include "timer.h"
#include "chipset.h"
#include "hwtimer.h"
#include "math_util.h"
#include "util.h"
#include "i2c_hid.h"
#include "i2c_hid_mediakeys.h"
/* Chip specific */
#include "registers.h"

#define HID_SLAVE_CTRL 3

#define REPORT_ID_RADIO		0x01
#define REPORT_ID_CONSUMER	0x02
#define REPORT_ID_SENSOR	0x03
#define REPORT_ID_SYSTEM	0x04

#define ALS_REPORT_STOP		0x00
#define ALS_REPORT_POLLING	0x01
#define ALS_REPORT_THRES	0x02

/*
 * See hid usage tables for consumer page
 * https://www.usb.org/hid
 */
#define BUTTON_ID_BRIGHTNESS_INCREMENT 0x006F
#define BUTTON_ID_BRIGHTNESS_DECREMENT 0x0070

#define EVENT_HID_HOST_IRQ	0x8000
#define EVENT_REPORT_ILLUMINANCE_VALUE	0x4000

#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_KEYBOARD, format, ## args)

static uint8_t key_states[HID_KEY_MAX];
static uint32_t update_key;

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

struct system_report {
	uint8_t state;
} __packed;

enum system_buttons: uint8_t {
	SYSTEM_KEY_FN = BIT(0),
	SYSTEM_KEY_FN_LOCK = BIT(1),
	SYSTEM_FN_LOCK_INDICATOR = BIT(2),
	SYSTEM_KEY_DISPLAY_TOGGLE = BIT(3),
};

static struct radio_report radio_button;
static struct consumer_button_report consumer_button;
static struct als_input_report als_sensor;
static struct als_feature_report als_feature;
static struct system_report system_buttons;

int update_hid_key(enum media_key key, bool pressed)
{
	if (key >= HID_KEY_MAX) {
		return EC_ERROR_INVAL;
	}
	if (key_states[key] != pressed) {
		key_states[key] = pressed;
		task_set_event(TASK_ID_HID, 1 << key, 0);
	}

	return EC_SUCCESS;
}

/* Called on AP S5 -> S3 transition */
static void hid_startup(void)
{
	/*reset after lines go high*/
	MCHP_I2C_CTRL(HID_SLAVE_CTRL) = BIT(7) |
					BIT(6) |
					BIT(3) |
					BIT(0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP,
		hid_startup,
		HOOK_PRIO_DEFAULT);

/* HID input report descriptor
 *
 * For a complete reference, please see the following docs on usb.org
 *
 * 1. Device Class Definition for HID
 * 2. HID Usage Tables
 */

static const uint8_t report_desc[] = {
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
	0x55, 0x0E,		/* UNIT EXPONENT (0x0E) */
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
	0x55, 0x00,		/* UNIT EXPONENT (0x00) */
	0x75, 0x10,		/* Report Size (16) */
	0x95, 0x01,		/* Report Count (1) */
	0x81, 0x02,		/* Input (Data,Arr,Abs) */
	0xC0,			/* END_COLLECTION */

	/* System Control Collection */
	0x05, 0x01,		/* USAGE_PAGE (Generic Desktop) */
	0x09, 0x80,		/* USAGE (System Control) */
	0xA1, 0x01,		/* COLLECTION (Application) */
	0x85, REPORT_ID_SYSTEM,	/* Report ID (System) */
	0x15, 0x00,		/* LOGICAL_MINIMUM (0) */
	0x25, 0x01,		/* LOGICAL_MAXIMUM (1) */
	0x95, 0x01,		/* REPORT_COUNT (1) */
	0x75, 0x01,		/* REPORT_SIZE (1) */
	0x09, 0x97,		/* USAGE (System Function Shift) */
	0x81, 0x02,		/* INPUT (Data,Var,Abs) */
	0x09, 0x98,		/* USAGE (System Function Shift Lock) */
	0x81, 0x06,		/* INPUT (Data,Var,Rel) */
	0x09, 0x99,		/* USAGE (System Function Shift Lock Indicator) */
	0x81, 0x02,		/* INPUT (Data,Var,Abs) */
	0x09, 0xB5,		/* USAGE (System Display Toggle Int/Ext Mode) */
	0x81, 0x06,		/* INPUT (Data,Var,Rel) */
	0x75, 0x04,		/* REPORT_SIZE (4) */
	0x81, 0x03,		/* INPUT (Cnst,Var,Abs) */
	0xC0,			/* END_COLLECTION */
};




static struct i2c_hid_descriptor hid_desc = {
	.wHIDDescLength = I2C_HID_DESC_LENGTH,
	.bcdVersion = I2C_HID_BCD_VERSION,
	.wReportDescLength = sizeof(report_desc),
	.wReportDescRegister = I2C_HID_REPORT_DESC_REGISTER,
	.wInputRegister = I2C_HID_INPUT_REPORT_REGISTER,
	.wMaxInputLength = I2C_HID_HEADER_SIZE +
			   sizeof(struct als_input_report), /*Note if there are multiple reports this has to be max*/
	.wOutputRegister = 0,
	.wMaxOutputLength = 0,
	.wCommandRegister = I2C_HID_COMMAND_REGISTER,
	.wDataRegister = I2C_HID_DATA_REGISTER,
	.wVendorID = I2C_HID_MEDIAKEYS_VENDOR_ID,
	.wProductID = I2C_HID_MEDIAKEYS_PRODUCT_ID,
	.wVersionID = I2C_HID_MEDIAKEYS_FW_VERSION,
};

/*
 * In I2C HID, the host would request for an input report immediately following
 * the protocol initialization. The device is required to respond with exactly
 * 2 empty bytes. Furthermore, some hosts may use a single byte SMBUS read to
 * check if the device exists on the specified I2C address.
 *
 * These variables record if such probing/initialization have been done before.
 */
static bool pending_probe;
static bool pending_reset;

/* Current active report buffer index */
static int report_active_index;

/* Current input mode */
static uint8_t input_mode;


void i2c_hid_mediakeys_init(void)
{
	input_mode = 0;
	report_active_index = 0;

	/* Respond probing requests for now. */
	pending_probe = false;
	pending_reset = false;
}

void i2c_hid_als_init(void)
{
	als_feature.connection_type = HID_INTEGRATED;
	als_feature.reporting_state = HID_ALL_EVENTS;
	als_feature.power_state = HID_D0_FULL_POWER;
	als_feature.sensor_state = HID_READY;
	als_feature.report_interval = 100;
	als_feature.sensitivity = HID_ALS_SENSITIVITY;
	als_feature.maximum = HID_ALS_MAX;
	als_feature.minimum = HID_ALS_MIN;

	als_sensor.event_type = 0x04; /* HID_DATA_UPDATED */
	als_sensor.sensor_state = 0x02; /* HID READY */
	als_sensor.illuminanceValue = 0x0000;
}

static int als_polling_mode_count;
void report_illuminance_value(void)
{
	uint16_t newIlluminaceValue = *(uint16_t *)host_get_memmap(EC_MEMMAP_ALS);
	static int granularity;

	/* We need to polling the ALS value at least 6 seconds */
	if (als_polling_mode_count <= 60) {
		als_polling_mode_count++; /* time base 100ms */

		/* bypass the EC_MEMMAP_ALS value to input report */
		als_sensor.illuminanceValue = newIlluminaceValue;
		task_set_event(TASK_ID_HID, ((1 << HID_ALS_REPORT_LUX) |
			EVENT_REPORT_ILLUMINANCE_VALUE), 0);
	} else {
		if (ABS(als_sensor.illuminanceValue - newIlluminaceValue) > granularity) {
			als_sensor.illuminanceValue = newIlluminaceValue;
			task_set_event(TASK_ID_HID, ((1 << HID_ALS_REPORT_LUX) |
				EVENT_REPORT_ILLUMINANCE_VALUE), 0);
		} else {
			task_set_event(TASK_ID_HID, EVENT_REPORT_ILLUMINANCE_VALUE, 0);
		}
	}

	/**
	 * To ensure the best experience the ALS should have a granularity of
	 * at most 1 lux when the ambient light is below 25 lux and a granularity
	 * of at most 4% of the ambient light when it is above 25 lux.
	 * This enable the adaptive brightness algorithm to perform smooth screen
	 * brightness transitions.
	 */
	if (newIlluminaceValue < 25)
		granularity = 1;
	else
		granularity = newIlluminaceValue*4/100;

}
DECLARE_DEFERRED(report_illuminance_value);

static void i2c_hid_send_response(void)
{
		task_set_event(TASK_ID_HID, EVENT_HID_HOST_IRQ, 0);
}

static void als_report_control(uint8_t report_mode)
{
	if (report_mode == 0x01) {
		/* als report mode = polling */
		hook_call_deferred(&report_illuminance_value_data,
			((int) als_feature.report_interval) * MSEC);
	} else if (report_mode == 0x02) {
		/* als report mode = threshold */
		;
	} else {
		/* stop report als value */
		hook_call_deferred(&report_illuminance_value_data, -1);
		als_polling_mode_count = 0;
	}
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

static void extract_report(size_t len, const uint8_t *buffer, void *data,
			   size_t data_len)
{
	if (len != 9 + data_len) {
		CPRINTS("I2C-HID: SET_REPORT buffer length mismatch");
		return;
	}
	memcpy(data, buffer + 9, data_len);
}

static int i2c_hid_touchpad_command_process(size_t len, uint8_t *buffer)
{
	uint8_t command = buffer[3] & 0x0F;
	uint8_t power_state = buffer[2] & 0x03;
	uint8_t report_id = buffer[2] & 0x0F;
	uint8_t report_type = (buffer[2] & 0x30) >> 4;
	size_t response_len = 0;

	switch (command) {
	case I2C_HID_CMD_RESET:
		i2c_hid_mediakeys_init();

		/* Wait for the 2-bytes I2C read following the protocol reset. */
		pending_probe = false;
		pending_reset = true;
		input_mode = REPORT_ID_RADIO;
		i2c_hid_send_response();
		break;
	case I2C_HID_CMD_GET_REPORT:
		switch (report_id) {
		case REPORT_ID_RADIO:
			response_len =
				fill_report(buffer, report_id,
						&radio_button,
						sizeof(struct radio_report));
			break;
		case REPORT_ID_CONSUMER:
			response_len =
				fill_report(buffer, report_id,
						&consumer_button,
						sizeof(struct consumer_button_report));
			break;
		case REPORT_ID_SENSOR:
			if (report_type == 0x01) {
				response_len =
					fill_report(buffer, report_id,
						&als_sensor,
						sizeof(struct als_input_report));
			} else if (report_type == 0x03) {
				response_len =
					fill_report(buffer, report_id,
						&als_feature,
						sizeof(struct als_feature_report));
			}
			break;
		case REPORT_ID_SYSTEM:
			response_len =
				fill_report(buffer, report_id,
						&system_buttons,
						sizeof(struct system_report));
			break;
		default:
			response_len = 2;
			buffer[0] = response_len;
			buffer[1] = 0;
			break;
		}
		break;
	case I2C_HID_CMD_SET_REPORT:
		switch (report_id) {
		case REPORT_ID_SENSOR:
			extract_report(len, buffer, &als_feature, sizeof(struct als_feature_report));
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
		*buffer = power_state;
		response_len = 1;

		if (power_state == 0x00) {
			i2c_hid_als_init();
			als_report_control(ALS_REPORT_POLLING);
		} else
			als_report_control(ALS_REPORT_STOP);

		break;
	default:
		return 0;
	}
	return response_len;
}

int i2c_hid_process(unsigned int len, uint8_t *buffer)
{
	size_t response_len = 0;
	int reg;

	if (len == 0)
		reg = I2C_HID_INPUT_REPORT_REGISTER;
	else
		reg = UINT16_FROM_BYTE_ARRAY_LE(buffer, 0);

	switch (reg) {
	case I2C_HID_MEDIAKEYS_HID_DESC_REGISTER:
		memcpy(buffer, &hid_desc, sizeof(hid_desc));
		response_len = sizeof(hid_desc);
		break;
	case I2C_HID_REPORT_DESC_REGISTER:
		memcpy(buffer, report_desc, sizeof(report_desc));
		response_len = sizeof(report_desc);
		break;
	case I2C_HID_INPUT_REPORT_REGISTER:
		/* Single-byte read probing. */
		if (pending_probe) {
			buffer[0] = 0;
			response_len = 1;
			break;
		}
		/* Reset protocol: 2 empty bytes. */
		if (pending_reset) {
			pending_reset = false;
			memset(buffer, 0, hid_desc.wMaxInputLength);
			response_len = hid_desc.wMaxInputLength;
			break;
		}
		/* Common input report requests. */
		if (input_mode == REPORT_ID_RADIO) {
			response_len =
				fill_report(buffer, REPORT_ID_RADIO,
						&radio_button,
						sizeof(struct radio_report));
		} else if (input_mode == REPORT_ID_CONSUMER) {
			response_len =
				fill_report(buffer, REPORT_ID_CONSUMER,
						&consumer_button,
						sizeof(struct consumer_button_report));
		} else if (input_mode == REPORT_ID_SENSOR) {
			response_len =
				fill_report(buffer, REPORT_ID_SENSOR,
						&als_sensor,
						sizeof(struct als_input_report));
		} else if (input_mode == REPORT_ID_SYSTEM) {
			response_len =
				fill_report(buffer, REPORT_ID_SYSTEM,
						&system_buttons,
						sizeof(struct system_report));
		} 
		break;
	case I2C_HID_COMMAND_REGISTER:
		response_len = i2c_hid_touchpad_command_process(len, buffer);
		break;
	default:
		/* Unknown register has been received. */
		return 0;
	}
	return response_len;
}

/*I2C Write from master */
void i2c_data_received(int port, uint8_t *buf, int len)
{

	i2c_hid_process(len, buf);
	task_set_event(TASK_ID_HID, TASK_EVENT_I2C_IDLE, 0);
}
/* I2C Read from master */
/* CTS I2C protocol implementation */
int i2c_set_response(int port, uint8_t *buf, int len)
{
	int ret = 0;

	ret = i2c_hid_process(len, buf);

	gpio_set_level(GPIO_SOC_EC_INT_L, 1);

	task_set_event(TASK_ID_HID, TASK_EVENT_I2C_IDLE, 0);
	return ret;
}

void hid_irq_to_host(void)
{
	uint32_t i2c_evt;
	int timeout = 0;
	gpio_set_level(GPIO_SOC_EC_INT_L, 0);

	/* wait for host to perform i2c transaction or timeout
	 * this happens in an interrupt context, so the interrupt will handle
	 * the data request and ack a task event signifying we are done.
	 */
	i2c_evt = task_wait_event_mask(TASK_EVENT_I2C_IDLE, 100*MSEC);
	if (i2c_evt & TASK_EVENT_TIMER) {
		CPRINTS("I2CHID no host response");
	} else {
		/*CPRINTS("I2CHID host handled response");*/

	}
	/* wait for bus to be not busy */
	while (((MCHP_I2C_STATUS(HID_SLAVE_CTRL) & BIT(0)) == 0) && ++timeout < 1000) {
		usleep(10);
	}
	/*Deassert interrupt */
	gpio_set_level(GPIO_SOC_EC_INT_L, 1);
	usleep(10);
}

void hid_handler_task(void *p)
{
	uint32_t event;
	size_t i;
	i2c_hid_mediakeys_init();
	while (1) {
		event = task_wait_event(-1);
		if (event & TASK_EVENT_I2C_IDLE) {
			/* TODO host is requesting data from device */
		}
		if (event & EVENT_HID_HOST_IRQ) {
			hid_irq_to_host();
		}

		if (event & EVENT_REPORT_ILLUMINANCE_VALUE) {
			/* start reporting illuminance value in S0*/
			hook_call_deferred(&report_illuminance_value_data,
					((int) als_feature.report_interval) * MSEC);
        }

		if (event & 0xFFFF) {

			for (i = 0; i < 15; i++) {
				/**
				 * filter EVENT_REPORT_ILLUMINANCE_VALUE
				 * this event only call deferred
				 */
				if ((event & 0xBFFF) & (1<<i)) {
					update_key = i;
					switch (i) {
					case HID_KEY_DISPLAY_BRIGHTNESS_UP:
						input_mode = REPORT_ID_CONSUMER;
						if (key_states[i]) {
							consumer_button.button_id = BUTTON_ID_BRIGHTNESS_INCREMENT;
						} else {
							consumer_button.button_id = 0;
						}
						break;
					case HID_KEY_DISPLAY_BRIGHTNESS_DN:
						input_mode = REPORT_ID_CONSUMER;
						if (key_states[i]) {
							consumer_button.button_id = BUTTON_ID_BRIGHTNESS_DECREMENT;
						} else {
							consumer_button.button_id = 0;
						}
						break;
					case HID_KEY_AIRPLANE_MODE:
						input_mode = REPORT_ID_RADIO;
							radio_button.state = key_states[i] ? 1 : 0;
						break;
					case HID_KEY_FN:
						input_mode = REPORT_ID_SYSTEM;
						if (key_states[i]) {
							system_buttons.state |= SYSTEM_KEY_FN;
						} else {
							system_buttons.state &= ~SYSTEM_KEY_FN;
						}
						break;
					case HID_KEY_FN_LOCK:
						input_mode = REPORT_ID_SYSTEM;
						if (key_states[i]) {
							system_buttons.state |= SYSTEM_KEY_FN_LOCK;
						} else {
							system_buttons.state &= ~SYSTEM_KEY_FN_LOCK;
						}
						break;
					case HID_FN_LOCK_INDICATOR:
						input_mode = REPORT_ID_SYSTEM;
						if (key_states[i]) {
							system_buttons.state |= SYSTEM_FN_LOCK_INDICATOR;
						} else {
							system_buttons.state &= ~SYSTEM_FN_LOCK_INDICATOR;
						}
						break;
					case HID_KEY_DISPLAY_TOGGLE:
						input_mode = REPORT_ID_SYSTEM;
						if (key_states[i]) {
							system_buttons.state |= SYSTEM_KEY_DISPLAY_TOGGLE;
						} else {
							system_buttons.state &= ~SYSTEM_KEY_DISPLAY_TOGGLE;
						}
						break;
					case HID_ALS_REPORT_LUX:

						input_mode = REPORT_ID_SENSOR;
						break;
					}
					/* we don't need to assert the interrupt when system state in S0ix */
					if (chipset_in_state(CHIPSET_STATE_ON))
						hid_irq_to_host();
				}
			}
		}
	}
};
