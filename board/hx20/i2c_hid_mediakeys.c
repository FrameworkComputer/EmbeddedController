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

#include "hwtimer.h"
#include "util.h"
#include "i2c_hid.h"
#include "i2c_hid_mediakeys.h"
/* Chip specific */
#include "registers.h"

#define HID_SLAVE_CTRL 3

#define REPORT_ID_RADIO				0x01
#define REPORT_ID_CONSUMER		    0x02
#define REPORT_ID_KEYBOARD_BACKLIGHT	0x05

/*
 * See hid usage tables for consumer page
 * https://www.usb.org/hid
 */
#define BUTTON_ID_BRIGHTNESS_INCREMENT 0x006F
#define BUTTON_ID_BRIGHTNESS_DECREMENT 0x0070

#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_KEYBOARD, format, ## args)

static uint8_t key_states[HID_KEY_MAX];
static uint32_t update_key;
/*
 *
 *
 */

struct radio_report {
	uint8_t state;
} __packed;

struct consumer_button_report {
	uint16_t button_id;
} __packed;

struct keyboard_backlight_report {
	uint8_t level;
} __packed;

static struct radio_report radio_button;
static struct consumer_button_report consumer_button;
static struct keyboard_backlight_report keyboard_backlight;


int update_hid_key(enum media_key key, bool pressed)
{
	if (key >= HID_KEY_MAX) {
		return EC_ERROR_INVAL;
	}
	if (key == HID_KEY_AIRPLANE_MODE || key == HID_KEY_KEYBOARD_BACKLIGHT) {
		key_states[key] = pressed;
		if (pressed)
			task_set_event(TASK_ID_HID, 1 << key, 0);
	} else if (key_states[key] != pressed) {
		key_states[key] = pressed;
		task_set_event(TASK_ID_HID, 1 << key, 0);
	}

	return EC_SUCCESS;
}


void kblight_update_hid(uint8_t percent)
{
	keyboard_backlight.level = percent;
	update_hid_key(HID_KEY_KEYBOARD_BACKLIGHT, 1);
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
	0x05, 0x01,	/* USAGE_PAGE (Generic Desktop) */
	0x09, 0x0C,	/* USAGE (Wireless Radio Controls) */
	0xA1, 0x01,	/*   COLLECTION (Application) */
	0x85, REPORT_ID_RADIO,		/* Report ID (Radio) */
	0x15, 0x00,	/*     LOGICAL_MINIMUM (0) */
	0x25, 0x01,	/*     LOGICAL_MAXIMUM (1) */
	0x09, 0xC6,	/*     USAGE (Wireless Radio Button) */
	0x95, 0x01,	/*     REPORT_COUNT (1) */
	0x75, 0x01,	/*     REPORT_SIZE (1) */
	0x81, 0x06,	/*     INPUT (Data,Var,Rel) */
	0x75, 0x07,	/*     REPORT_SIZE (7) */
	0x81, 0x03,	/*     INPUT (Cnst,Var,Abs) */
	0xC0,	    /*   END_COLLECTION */

	/* Consumer controls collection */
	0x05, 0x0C,	/* USAGE_PAGE (Consumer Devices) */
	0x09, 0x01,	/* USAGE (Consumer Control) */
	0xA1, 0x01,	/*   COLLECTION (Application) */
	0x85, REPORT_ID_CONSUMER,		/* Report ID (Consumer) */
	0x15, 0x00,	/*     LOGICAL_MINIMUM (0x0) */
	0x26, 0xFF, 0x03,	/*     LOGICAL_MAXIMUM (0x3FF) */
	0x19, 0x00,	/*     Usage Minimum (0) */
	0x2A, 0xFF, 0x03,	/*     Usage Maximum (0) */
	0x75, 0x10,	/*     Report Size (16) */
	0x95, 0x01,	/*     Report Count (1) */
	0x81, 0x00,	/*     Input (Data,Arr,Abs) */
	0xC0,	    /*   END_COLLECTION */

	/* Keyboard Backlight Level Collection */
	0x05, 0x01,		/* USAGE_PAGE (Generic Desktop) */
	0x09, 0x06,		/* USAGE (Keyboard) */
	0xA1, 0x01,		/* COLLECTION (Application) */
	0x85, REPORT_ID_KEYBOARD_BACKLIGHT,	/* Report ID (Keyboard Backlight) */
	0x05, 0x0C,		/* USAGE_PAGE (Consumer Devices) */
	0x09, 0x7B,		/* USAGE (Keyboard Backlight Set Level) */
	0x15, 0x00,		/* LOGICAL_MINIMUM (0) */
	0x25, 0x64,		/* LOGICAL_MAXIMUM (100) */
	0x95, 0x01,		/* REPORT_COUNT (1) */
	0x75, 0x08,		/* REPORT_SIZE (8) */
	0x81, 0x02,		/* INPUT (Data,Var,Abs) */
	0xC0,	
};




static struct i2c_hid_descriptor hid_desc = {
	.wHIDDescLength = I2C_HID_DESC_LENGTH,
	.bcdVersion = I2C_HID_BCD_VERSION,
	.wReportDescLength = sizeof(report_desc),
	.wReportDescRegister = I2C_HID_REPORT_DESC_REGISTER,
	.wInputRegister = I2C_HID_INPUT_REPORT_REGISTER,
	.wMaxInputLength = I2C_HID_HEADER_SIZE +
			   sizeof(struct consumer_button_report), /*Note if there are multiple reports this has to be max*/
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

static void i2c_hid_send_response(void)
{
		task_set_event(TASK_ID_HID, 0x8000, 0);
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

static int i2c_hid_touchpad_command_process(size_t len, uint8_t *buffer)
{
	uint8_t command = buffer[3] & 0x0F;
	uint8_t power_state = buffer[2] & 0x03;
	uint8_t report_id = buffer[2] & 0x0F;
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
			CPRINTF("HID RPT");

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
		case REPORT_ID_KEYBOARD_BACKLIGHT:
			response_len =
				fill_report(buffer, report_id,
						&keyboard_backlight,
						sizeof(struct keyboard_backlight_report));
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
		/*
		case REPORT_ID_INPUT_MODE:
			extract_report(len, buffer, &input_mode,
						sizeof(input_mode));
			break;
		case REPORT_ID_REPORTING:
			extract_report(len, buffer, &reporting,
						sizeof(reporting));
			break;
		*/
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
		memcpy(buffer, &report_desc, sizeof(report_desc));
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
			buffer[0] = 0;
			buffer[1] = 0;
			response_len = 2;
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
		} else if (input_mode == REPORT_ID_KEYBOARD_BACKLIGHT) {
			response_len =
				fill_report(buffer, REPORT_ID_KEYBOARD_BACKLIGHT,
						&keyboard_backlight,
						sizeof(struct keyboard_backlight_report));
		};
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
		if (event & 0x8000) {
			hid_irq_to_host();
		}
		if (event & 0xFFFF) {

			for (i = 0; i < 15; i++) {
				if (event & (1<<i)) {
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
					case HID_KEY_KEYBOARD_BACKLIGHT:
						input_mode = REPORT_ID_KEYBOARD_BACKLIGHT;
						break;
					}
					hid_irq_to_host();
				}
			}
		}
	}
};
