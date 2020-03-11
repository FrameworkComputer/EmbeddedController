/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "i2c_hid_touchpad.h"

#include "console.h"
#include "hwtimer.h"
#include "util.h"

/* 2 bytes for length + 1 byte for report ID */
#define I2C_HID_HEADER_SIZE			3

/* Report ID */
#define REPORT_ID_TOUCH				0x01
#define REPORT_ID_MOUSE				0x02
#define REPORT_ID_DEVICE_CAPS			0x0A
#define REPORT_ID_DEVICE_CERT			0x0B
#define REPORT_ID_INPUT_MODE			0x0C
#define REPORT_ID_REPORTING			0x0D

#define INPUT_MODE_MOUSE			0x00
#define INPUT_MODE_TOUCH			0x03

/* VID/PID/FW version */
#if !defined(I2C_HID_TOUCHPAD_VENDOR_ID) || \
    !defined(I2C_HID_TOUCHPAD_PRODUCT_ID) || \
    !defined(I2C_HID_TOUCHPAD_FW_VERSION)
#error "Must define touchpad VID/PID/FW version"
#endif
/*
 * Touchpad properties
 *
 * Physical dimensions are in the unit of mms.
 */
#if !defined(I2C_HID_TOUCHPAD_MAX_X) || \
    !defined(I2C_HID_TOUCHPAD_MAX_Y) || \
    !defined(I2C_HID_TOUCHPAD_MAX_PHYSICAL_X) || \
    !defined(I2C_HID_TOUCHPAD_MAX_PHYSICAL_Y)
#error "Must define finger maximum X/Y and physical dimensions"
#endif
/*
 * Maximum width/height of the contact (i.e., touch major/minor in Linux MT-B)
 *
 * According to the Linux's MT protocol, the max value of touch major/minor
 * should be sqrt(X^2+Y^2). However, this is rarely implemented by touchpads
 * in practice. Touchpads often output major/minor in custom units with very
 * different data ranges. It is therefore recommended for the user to check the
 * device's spec and set these values manually.
 */
#if !defined(I2C_HID_TOUCHPAD_MAX_WIDTH) || \
    !defined(I2C_HID_TOUCHPAD_MAX_HEIGHT) || \
    !defined(I2C_HID_TOUCHPAD_MAX_PRESSURE)
#error "Must define finger maximum width/height/pressure"
#endif
/*
 * The touchpad is expected to provide at least the horizontal/vertical status
 * for each contact (if one is wider than its height). This can be computed
 * simply as bool(WIDTH>HEIGHT).
 */
#ifndef I2C_HID_TOUCHPAD_MAX_ORIENTATION
#error "Must define finger maximum orientation value"
#endif
/*
 * Conversion factor between the finger movement and the mouse cursor movement.
 * This is a bit similar to the mouse CPI and is used by mouse reports only.
 */
#if !defined(I2C_HID_TOUCHPAD_MOUSE_SCALE_X) || \
    !defined(I2C_HID_TOUCHPAD_MOUSE_SCALE_Y)
#error "Must define mouse horizontal/vertical scaling factors"
#endif

/* Helper bit-op macros */
#define N_BITS(n) \
( \
		(n) < (1 << 1) ? 1 : \
		(n) < (1 << 2) ? 2 : \
		(n) < (1 << 3) ? 3 : \
		(n) < (1 << 4) ? 4 : \
		(n) < (1 << 5) ? 5 : \
		(n) < (1 << 6) ? 6 : \
		(n) < (1 << 7) ? 7 : \
		(n) < (1 << 8) ? 8 : \
		(n) < (1 << 9) ? 9 : \
		(n) < (1 << 10) ? 10 : \
		(n) < (1 << 11) ? 11 : \
		(n) < (1 << 12) ? 12 : \
		(n) < (1 << 13) ? 13 : \
		(n) < (1 << 14) ? 14 : \
		(n) < (1 << 15) ? 15 : \
		16 \
)
/* We would need to pad some bits at the end of each finger struct to match
 * the allocation unit's boundary so the array indexing may work correctly.
 */
#define N_VAR_BITS \
( \
		N_BITS(I2C_HID_TOUCHPAD_MAX_X) + \
		N_BITS(I2C_HID_TOUCHPAD_MAX_Y) + \
		N_BITS(I2C_HID_TOUCHPAD_MAX_WIDTH) + \
		N_BITS(I2C_HID_TOUCHPAD_MAX_HEIGHT) + \
		N_BITS(I2C_HID_TOUCHPAD_MAX_PRESSURE) + \
		N_BITS(I2C_HID_TOUCHPAD_MAX_ORIENTATION) \
)
#define N_PADDING_BITS ((DIV_ROUND_UP(N_VAR_BITS, 8) * 8) - N_VAR_BITS)
#define N_BITS_ORIENTATION \
	(N_BITS(I2C_HID_TOUCHPAD_MAX_ORIENTATION) + N_PADDING_BITS)
/* Structs for holding input report data
 *
 * These need to be modified in correspondence with the HID input report
 * descriptor below.
 *
 * The HID usage names differ from the Evdev event names in some cases. For
 * example, touch major/minor are put under width/height and orientation is
 * called azimuth.
 */
struct finger {
	/*
	 * Whether a finger is intentional or not. This could be used to
	 * identify unintended contacts or palms but is up to the OS
	 * explanation.
	 */
	uint8_t confidence:1;
	/*
	 * Whether a finger is touching the surface (leaving/left finger gets
	 * 0).
	 */
	uint8_t tip:1;
	/*
	 * Whether a finger is within the sensor range. For example, hovering
	 * fingers would have tip=0 and inrange=1.
	 */
	uint8_t inrange:1;
	/*
	 * Contact id. This is like slot numbers in Linux MT-B.
	 */
	uint8_t id:5;
	uint16_t x:N_BITS(I2C_HID_TOUCHPAD_MAX_X);
	uint16_t y:N_BITS(I2C_HID_TOUCHPAD_MAX_Y);
	uint16_t width:N_BITS(I2C_HID_TOUCHPAD_MAX_WIDTH);
	uint16_t height:N_BITS(I2C_HID_TOUCHPAD_MAX_HEIGHT);
	uint16_t pressure:N_BITS(I2C_HID_TOUCHPAD_MAX_PRESSURE);
	uint16_t orientation:N_BITS_ORIENTATION;
} __packed;

struct touch_report {
	uint8_t button:1;
	uint8_t count:7;
	uint16_t timestamp;
	struct finger finger[I2C_HID_TOUCHPAD_MAX_FINGERS];
} __packed;

struct mouse_report {
	uint8_t button1:1;
	/* Windows expects at least two button usages in a mouse report. Many
	 * touchpads on the Chromebook are a single clickable surface, so
	 * button2 isn't used. That said, we may later report a button2 event if
	 * necessary.
	 */
	uint8_t button2:1;
	uint8_t unused:6;
	int8_t x;
	int8_t y;
} __packed;

/* HID input report descriptor
 *
 * For a complete reference, please see the following docs on usb.org
 *
 * 1. Device Class Definition for HID
 * 2. HID Usage Tables
 */
static const uint8_t report_desc[] = {
	/* Mouse Collection */
	0x05, 0x01,			/* Usage Page (Generic Desktop) */
	0x09, 0x02,			/* Usage (Mouse) */
	0xA1, 0x01,			/* Collection (Application) */
	0x85, REPORT_ID_MOUSE,		/* Report ID (Mouse) */
	0x09, 0x01,			/*   Usage (Pointer) */
	0xA1, 0x00,			/*   Collection (Physical) */
	0x05, 0x09,			/*     Usage Page (Button) */
	0x19, 0x01,			/*     Usage Minimum (Button 1) */
	0x29, 0x02,			/*     Usage Maximum (Button 2) */
	0x15, 0x00,			/*     Logical Minimum (0) */
	0x25, 0x01,			/*     Logical Maximum (1) */
	0x75, 0x01,			/*     Report Size (1) */
	0x95, 0x02,			/*     Report Count (2) */
	0x81, 0x02,			/*     Input (Data,Var,Abs) */
	0x95, 0x06,			/*     Report Count (6) */
	0x81, 0x03,			/*     Input (Cnst,Var,Abs) */
	0x05, 0x01,			/*     Usage Page (Generic Desktop) */
	0x09, 0x30,			/*     Usage (X) */
	0x09, 0x31,			/*     Usage (Y) */
	0x15, 0x81,			/*     Logical Minimum (-127) */
	0x25, 0x7F,			/*     Logical Maximum (127) */
	0x75, 0x08,			/*     Report Size (8) */
	0x95, 0x02,			/*     Report Count (2) */
	0x81, 0x06,			/*     Input (Data,Var,Rel) */
	0xC0,				/*   End Collection */
	0xC0,				/* End Collection */

	/* Touchpad Collection */
	0x05, 0x0D,			/* Usage Page (Digitizer) */
	0x09, 0x05,			/* Usage (Touch Pad) */
	0xA1, 0x01,			/* Collection (Application) */
	0x85, REPORT_ID_TOUCH,		/*   Report ID (Touch) */

	/* Button */
	0x05, 0x09,			/*   Usage Page (Button) */
	0x19, 0x01,			/*   Usage Minimum (0x01) */
	0x29, 0x01,			/*   Usage Maximum (0x01) */
	0x15, 0x00,			/*   Logical Minimum (0) */
	0x25, 0x01,			/*   Logical Maximum (1) */
	0x75, 0x01,			/*   Report Size (1) */
	0x95, 0x01,			/*   Report Count (1) */
	0x81, 0x02,			/*   Input (Data,Var,Abs) */

	/* Contact count */
	0x05, 0x0D,			/*   Usage Page (Digitizer) */
	0x09, 0x54,			/*   Usage (Contact count) */
	0x25, I2C_HID_TOUCHPAD_MAX_FINGERS,	/* Logical Max. (MAX_FINGERS) */
	0x75, 0x07,			/*   Report Size (7) */
	0x95, 0x01,			/*   Report Count (1) */
	0x81, 0x02,			/*   Input (Data,Var,Abs) */

	/* Scan time */
	0x55, 0x0C,			/*   Unit Exponent (-4) */
	0x66, 0x01, 0x10,		/*   Unit (Seconds) */
	0x47, 0xFF, 0xFF, 0x00, 0x00,	/*   Physical Maximum (65535) */
	0x27, 0xFF, 0xFF, 0x00, 0x00,	/*   Logical Maximum (65535) */
	0x75, 0x10,			/*   Report Size (16) */
	0x95, 0x01,			/*   Report Count (1) */
	0x05, 0x0D,			/*   Usage Page (Digitizers) */
	0x09, 0x56,			/*   Usage (Scan Time) */
	0x81, 0x02,			/*   Input (Data,Var,Abs) */

#define FINGER(FINGER_NUMBER)						       \
	/* Finger FINGER_NUMBER */					       \
	0x05, 0x0D,			/*   Usage Page (Digitizer) */         \
	0x09, 0x22,			/*   Usage (Finger) */                 \
	0xA1, 0x02,			/*   Collection (Logical) */           \
	0x09, 0x47,			/*     Usage (Confidence) */           \
	0x09, 0x42,			/*     Usage (Tip Switch) */           \
	0x09, 0x32,			/*     Usage (In Range) */             \
	0x15, 0x00,			/*     Logical Minimum (0) */          \
	0x25, 0x01,			/*     Logical Maximum (1) */          \
	0x75, 0x01,			/*     Report Size (1) */              \
	0x95, 0x03,			/*     Report Count (3) */             \
	0x81, 0x02,			/*     Input (Data,Var,Abs) */         \
	0x09, 0x51,			/*     Usage (Contact identifier) */   \
	0x25, 0x1F,			/*     Logical Maximum (31) */         \
	0x75, 0x05,			/*     Report Size (5) */              \
	0x95, 0x01,			/*     Report Count (1) */             \
	0x81, 0x02,			/*     Input (Data,Var,Abs) */         \
	0x05, 0x01,			/*     Usage Page (Generic Desktop) */ \
	0x09, 0x30,			/*     Usage (X) */                    \
	0x55, 0x0E,			/*     Unit Exponent (-2) */           \
	0x65, 0x11,			/*     Unit (SI Linear, Length: cm) */ \
	0x35, 0x00,			/*     Physical Minimum (0) */         \
	0x46, I2C_HID_TOUCHPAD_MAX_PHYSICAL_X&0xff,			       \
	      I2C_HID_TOUCHPAD_MAX_PHYSICAL_X>>8,			       \
					/*     Physical Maximum */	       \
	0x26, I2C_HID_TOUCHPAD_MAX_X&0xff, I2C_HID_TOUCHPAD_MAX_X>>8,	       \
					/*     Logical Maximum */	       \
	0x75, N_BITS(I2C_HID_TOUCHPAD_MAX_X),				       \
					/*     Report Size */		       \
	0x81, 0x02,			/*     Input (Data,Var,Abs) */         \
	0x09, 0x31,			/*     Usage (Y) */                    \
	0x46, I2C_HID_TOUCHPAD_MAX_PHYSICAL_Y&0xff,			       \
	      I2C_HID_TOUCHPAD_MAX_PHYSICAL_Y>>8,			       \
					/*     Physical Maximum */	       \
	0x26, I2C_HID_TOUCHPAD_MAX_Y&0xff, I2C_HID_TOUCHPAD_MAX_Y>>8,	       \
					/*     Logical Maximum */	       \
	0x75, N_BITS(I2C_HID_TOUCHPAD_MAX_Y),				       \
					/*     Report Size */		       \
	0x81, 0x02,			/*     Input (Data,Var,Abs) */         \
	0x05, 0x0D,			/*     Usage Page (Digitizer) */       \
	0x09, 0x48,			/*     Usage (Width) */                \
	0x26, I2C_HID_TOUCHPAD_MAX_WIDTH&0xff, I2C_HID_TOUCHPAD_MAX_WIDTH>>8,  \
					/*     Logical Maximum */	       \
	0x75, N_BITS(I2C_HID_TOUCHPAD_MAX_WIDTH),			       \
					/*     Report Size */		       \
	0x81, 0x02,			/*     Input (Data,Var,Abs) */         \
	0x09, 0x49,			/*     Usage (Height) */               \
	0x26, I2C_HID_TOUCHPAD_MAX_HEIGHT&0xff, I2C_HID_TOUCHPAD_MAX_HEIGHT>>8,\
					/*     Logical Maximum */	       \
	0x75, N_BITS(I2C_HID_TOUCHPAD_MAX_HEIGHT),			       \
					/*     Report Size */		       \
	0x81, 0x02,			/*     Input (Data,Var,Abs) */         \
	0x09, 0x30,			/*     Usage (Tip pressure) */         \
	0x26, I2C_HID_TOUCHPAD_MAX_PRESSURE&0xff,			       \
	      I2C_HID_TOUCHPAD_MAX_PRESSURE>>8,				       \
					/*     Logical Maximum */	       \
	0x75, N_BITS(I2C_HID_TOUCHPAD_MAX_PRESSURE),			       \
					/*     Report Size */		       \
	0x81, 0x02,			/*     Input (Data,Var,Abs) */         \
	0x09, 0x3f,			/*     Usage (Azimuth Orientation) */  \
	0x16, 0x00, 0x00,		/*     Logical Minimum (0) */          \
	0x26, I2C_HID_TOUCHPAD_MAX_ORIENTATION&0xff,			       \
	      I2C_HID_TOUCHPAD_MAX_ORIENTATION>>8,			       \
					/*     Logical Maximum */	       \
	0x75, N_BITS_ORIENTATION,	/*     Report Size */		       \
	0x81, 0x02,			/*     Input (Data,Var,Abs) */         \
	0xC0,				/*   End Collection */

	FINGER(1)
	FINGER(2)
	FINGER(3)
	FINGER(4)
	FINGER(5)

#undef FINGER

	0x05, 0x0D,			/*   Usage Page (Digitizer) */
	0x85, REPORT_ID_DEVICE_CAPS,	/*   Report ID (Device Capabilities) */
	0x09, 0x55,			/*   Usage (Contact Count Maximum) */
	0x09, 0x59,			/*   Usage (Pad Type) */
	0x75, 0x08,			/*   Report Size (8) */
	0x95, 0x02,			/*   Report Count (2) */
	0x25, 0x0F,			/*   Logical Maximum (15) */
	0xB1, 0x02,			/*   Feature (Data,Var,Abs) */
	0x06, 0x00, 0xFF,		/*   Usage Page (Vendor Defined) */
	0x85, REPORT_ID_DEVICE_CERT,	/*   Report ID (Device Certification) */
	0x09, 0xC5,			/*   Usage (Vendor Usage 0xC5) */
	0x15, 0x00,			/*   Logical Minimum (0) */
	0x26, 0xFF, 0x00,		/*   Logical Maximum (255) */
	0x75, 0x08,			/*   Report Size (8) */
	0x96, 0x00, 0x01,		/*   Report Count (256) */
	0xB1, 0x02,			/*   Feature (Data,Var,Abs) */
	0xC0,				/* End Collection */

	/* Configuration Collection */
	0x05, 0x0D,			/* Usage Page (Digitizer) */
	0x09, 0x0E,			/* Usage (Configuration) */
	0xA1, 0x01,			/* Collection (Application) */
	0x85, REPORT_ID_INPUT_MODE,	/*   Report ID (Input Mode) */
	0x09, 0x22,			/*   Usage (Finger) */
	0xA1, 0x02,			/*   Collection (Logical) */
	0x09, 0x52,			/*     Usage (Input Mode) */
	0x15, 0x00,			/*     Logical Minimum (0) */
	0x25, 0x0F,			/*     Logical Maximum (15) */
	0x75, 0x08,			/*     Report Size (8) */
	0x95, 0x01,			/*     Report Count (1) */
	0xB1, 0x02,			/*     Feature (Data,Var,Abs) */
	0xC0,				/*   End Collection */
	0x09, 0x22,			/*   Usage (Finger) */
	0xA1, 0x00,			/*   Collection (Physical) */
	0x85, REPORT_ID_REPORTING,	/*     Report ID (Selective Reporting)*/
	0x09, 0x57,			/*     Usage (Surface Switch) */
	0x09, 0x58,			/*     Usage (Button Switch) */
	0x75, 0x04,			/*     Report Size (4) */
	0x95, 0x02,			/*     Report Count (2) */
	0x25, 0x01,			/*     Logical Maximum (1) */
	0xB1, 0x02,			/*     Feature (Data,Var,Abs) */
	0xC0,				/*   End Collection */
	0xC0,				/* End Collection */
};

static const uint8_t device_caps[] = {
	I2C_HID_TOUCHPAD_MAX_FINGERS,	/* Contact Count Maximum */
	0x00,				/* Pad Type: Depressible click-pad */
};

/* A 256-byte default blob for the 'device certification status' feature report
 * expected by Windows.
 */
static const uint8_t device_cert[] = {
	0xFC, 0x28, 0xFE, 0x84, 0x40, 0xCB, 0x9A, 0x87,
	0x0D, 0xBE, 0x57, 0x3C, 0xB6, 0x70, 0x09, 0x88,
	0x07, 0x97, 0x2D, 0x2B, 0xE3, 0x38, 0x34, 0xB6,
	0x6C, 0xED, 0xB0, 0xF7, 0xE5, 0x9C, 0xF6, 0xC2,
	0x2E, 0x84, 0x1B, 0xE8, 0xB4, 0x51, 0x78, 0x43,
	0x1F, 0x28, 0x4B, 0x7C, 0x2D, 0x53, 0xAF, 0xFC,
	0x47, 0x70, 0x1B, 0x59, 0x6F, 0x74, 0x43, 0xC4,
	0xF3, 0x47, 0x18, 0x53, 0x1A, 0xA2, 0xA1, 0x71,
	0xC7, 0x95, 0x0E, 0x31, 0x55, 0x21, 0xD3, 0xB5,
	0x1E, 0xE9, 0x0C, 0xBA, 0xEC, 0xB8, 0x89, 0x19,
	0x3E, 0xB3, 0xAF, 0x75, 0x81, 0x9D, 0x53, 0xB9,
	0x41, 0x57, 0xF4, 0x6D, 0x39, 0x25, 0x29, 0x7C,
	0x87, 0xD9, 0xB4, 0x98, 0x45, 0x7D, 0xA7, 0x26,
	0x9C, 0x65, 0x3B, 0x85, 0x68, 0x89, 0xD7, 0x3B,
	0xBD, 0xFF, 0x14, 0x67, 0xF2, 0x2B, 0xF0, 0x2A,
	0x41, 0x54, 0xF0, 0xFD, 0x2C, 0x66, 0x7C, 0xF8,
	0xC0, 0x8F, 0x33, 0x13, 0x03, 0xF1, 0xD3, 0xC1,
	0x0B, 0x89, 0xD9, 0x1B, 0x62, 0xCD, 0x51, 0xB7,
	0x80, 0xB8, 0xAF, 0x3A, 0x10, 0xC1, 0x8A, 0x5B,
	0xE8, 0x8A, 0x56, 0xF0, 0x8C, 0xAA, 0xFA, 0x35,
	0xE9, 0x42, 0xC4, 0xD8, 0x55, 0xC3, 0x38, 0xCC,
	0x2B, 0x53, 0x5C, 0x69, 0x52, 0xD5, 0xC8, 0x73,
	0x02, 0x38, 0x7C, 0x73, 0xB6, 0x41, 0xE7, 0xFF,
	0x05, 0xD8, 0x2B, 0x79, 0x9A, 0xE2, 0x34, 0x60,
	0x8F, 0xA3, 0x32, 0x1F, 0x09, 0x78, 0x62, 0xBC,
	0x80, 0xE3, 0x0F, 0xBD, 0x65, 0x20, 0x08, 0x13,
	0xC1, 0xE2, 0xEE, 0x53, 0x2D, 0x86, 0x7E, 0xA7,
	0x5A, 0xC5, 0xD3, 0x7D, 0x98, 0xBE, 0x31, 0x48,
	0x1F, 0xFB, 0xDA, 0xAF, 0xA2, 0xA8, 0x6A, 0x89,
	0xD6, 0xBF, 0xF2, 0xD3, 0x32, 0x2A, 0x9A, 0xE4,
	0xCF, 0x17, 0xB7, 0xB8, 0xF4, 0xE1, 0x33, 0x08,
	0x24, 0x8B, 0xC4, 0x43, 0xA5, 0xE5, 0x24, 0xC2,
};

#define MAX_SIZEOF(a, b) (sizeof(a) > sizeof(b) ? sizeof(a) : sizeof(b))

static struct i2c_hid_descriptor hid_desc = {
	.wHIDDescLength = I2C_HID_DESC_LENGTH,
	.bcdVersion = I2C_HID_BCD_VERSION,
	.wReportDescLength = sizeof(report_desc),
	.wReportDescRegister = I2C_HID_REPORT_DESC_REGISTER,
	.wInputRegister = I2C_HID_INPUT_REPORT_REGISTER,
	.wMaxInputLength = I2C_HID_HEADER_SIZE +
			   MAX_SIZEOF(struct touch_report, struct mouse_report),
	.wOutputRegister = 0,
	.wMaxOutputLength = 0,
	.wCommandRegister = I2C_HID_COMMAND_REGISTER,
	.wDataRegister = I2C_HID_DATA_REGISTER,
	.wVendorID = I2C_HID_TOUCHPAD_VENDOR_ID,
	.wProductID = I2C_HID_TOUCHPAD_PRODUCT_ID,
	.wVersionID = I2C_HID_TOUCHPAD_FW_VERSION,
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

/* Reports (double buffered) */
#define MAX_REPORT_CNT	2

static struct touch_report touch_reports[MAX_REPORT_CNT];
static struct mouse_report mouse_reports[MAX_REPORT_CNT];

/* Current active report buffer index */
static int report_active_index;

/* Current input mode */
static uint8_t input_mode;

/*
 * TODO(b/151693566): Selectively report surface contact and button state in
 * input reports based on |reporting.surface_switch| and
 * |reporting.button_switch|, respectively.
 */
struct selective_reporting {
	uint8_t surface_switch:4;
	uint8_t button_switch:4;
} __packed;

static struct selective_reporting reporting;

/* Function declarations */
static int i2c_hid_touchpad_command_process(size_t len, uint8_t *buffer,
					    void (*send_response)(int len),
					    uint8_t *data);

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

/*
 * Extracts report data from |buffer| into |data| for reports from the host.
 *
 * |buffer| is expected to contain the values written to the command register
 * followed by the values written to the data register, upon receiving a
 * SET_REPORT command, in the following byte sequence format:
 *
 *   00 30 - command register address (0x3000)
 *   xx    - report type and ID
 *   03    - SET_REPORT
 *   00 30 - data register address (0x3000)
 *   xx xx - length
 *   xx    - report ID
 *   xx... - report data
 *
 * Note that command register and data register have the same address. Also,
 * any report ID >= 15 requires an extra byte after the SET_REPORT byte, which
 * is not supported here as we don't have any report ID >= 15.
 *
 * In summary, we expect |buffer| contains at least 10 bytes where the report
 * data starts at buffer[9]. If |buffer| contains the incorrect number bytes,
 * we ignore the report.
 */
static void extract_report(size_t len, const uint8_t *buffer, void *data,
			   size_t data_len)
{
	if (len != 9 + data_len) {
		ccprints("I2C-HID: SET_REPORT buffer length mismatch");
		return;
	}
	memcpy(data, buffer + 9, data_len);
}

void i2c_hid_touchpad_init(void)
{
	input_mode = INPUT_MODE_MOUSE;
	reporting.surface_switch = 1;
	reporting.button_switch = 1;
	report_active_index = 0;

	// Respond probing requests for now.
	pending_probe = true;
	pending_reset = false;
}

int i2c_hid_touchpad_process(unsigned int len, uint8_t *buffer,
			     void (*send_response)(int len), uint8_t *data,
			     int *reg, int *cmd)
{
	size_t response_len;

	if (len == 0)
		*reg = I2C_HID_INPUT_REPORT_REGISTER;
	else
		*reg = UINT16_FROM_BYTE_ARRAY_LE(buffer, 0);

	*cmd = 0;
	switch (*reg) {
	case I2C_HID_HID_DESC_REGISTER:
		memcpy(buffer, &hid_desc, sizeof(hid_desc));
		send_response(sizeof(hid_desc));
		break;
	case I2C_HID_REPORT_DESC_REGISTER:
		memcpy(buffer, &report_desc, sizeof(report_desc));
		send_response(sizeof(report_desc));
		break;
	case I2C_HID_INPUT_REPORT_REGISTER:
		// Single-byte read probing.
		if (pending_probe) {
			buffer[0] = 0;
			send_response(1);
			break;
		}
		// Reset protocol: 2 empty bytes.
		if (pending_reset) {
			pending_reset = false;
			buffer[0] = 0;
			buffer[1] = 0;
			send_response(2);
			break;
		}
		// Common input report requests.
		if (input_mode == INPUT_MODE_TOUCH) {
			response_len =
				fill_report(buffer, REPORT_ID_TOUCH,
					    &touch_reports[report_active_index],
					    sizeof(struct touch_report));
		} else {
			response_len =
				fill_report(buffer, REPORT_ID_MOUSE,
					    &mouse_reports[report_active_index],
					    sizeof(struct mouse_report));
		}
		send_response(response_len);
		break;
	case I2C_HID_COMMAND_REGISTER:
		*cmd = i2c_hid_touchpad_command_process(len, buffer,
						       send_response, data);
		break;
	default:
		/* Unknown register has been received. */
		return EC_ERROR_INVAL;
	}
	/* Unknown command has been received. */
	if (*cmd < 0)
		return EC_ERROR_INVAL;
	return EC_SUCCESS;
}

static int i2c_hid_touchpad_command_process(size_t len, uint8_t *buffer,
					    void (*send_response)(int len),
					    uint8_t *data)
{
	uint8_t command = buffer[3] & 0x0F;
	uint8_t power_state = buffer[2] & 0x03;
	uint8_t report_id = buffer[2] & 0x0F;
	size_t response_len;

	switch (command) {
	case I2C_HID_CMD_RESET:
		i2c_hid_touchpad_init();
		// Wait for the 2-bytes I2C read following the protocol reset.
		pending_probe = false;
		pending_reset = true;
		break;
	case I2C_HID_CMD_GET_REPORT:
		switch (report_id) {
		case REPORT_ID_TOUCH:
			response_len =
				fill_report(buffer, report_id,
					    &touch_reports[report_active_index],
					    sizeof(struct touch_report));
			break;
		case REPORT_ID_MOUSE:
			response_len =
				fill_report(buffer, report_id,
					    &mouse_reports[report_active_index],
					    sizeof(struct mouse_report));
			break;
		case REPORT_ID_DEVICE_CAPS:
			response_len = fill_report(buffer, report_id,
						   &device_caps,
						   sizeof(device_caps));
			break;
		case REPORT_ID_DEVICE_CERT:
			response_len = fill_report(buffer, report_id,
						   &device_cert,
						   sizeof(device_cert));
			break;
		case REPORT_ID_INPUT_MODE:
			response_len = fill_report(buffer, report_id,
						   &input_mode,
						   sizeof(input_mode));
			break;
		case REPORT_ID_REPORTING:
			response_len = fill_report(buffer, report_id,
						   &reporting,
						   sizeof(reporting));
			break;
		default:
			response_len = 2;
			buffer[0] = response_len;
			buffer[1] = 0;
			break;
		}
		send_response(response_len);
		break;
	case I2C_HID_CMD_SET_REPORT:
		switch (report_id) {
		case REPORT_ID_INPUT_MODE:
			extract_report(len, buffer, &input_mode,
				       sizeof(input_mode));
			break;
		case REPORT_ID_REPORTING:
			extract_report(len, buffer, &reporting,
				       sizeof(reporting));
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
		*data = power_state;
		break;
	default:
		return -1;
	}
	return command;
}

void i2c_hid_compile_report(struct touchpad_event *event)
{
	/* Save report into back buffer */
	struct touch_report *touch = &touch_reports[report_active_index ^ 1];
	struct touch_report *touch_old = &touch_reports[report_active_index];
	struct mouse_report *mouse = &mouse_reports[report_active_index ^ 1];
	int contact_num = 0;

	/* Touch report. */
	memset(touch, 0, sizeof(struct touch_report));
	for (int i = 0; i < I2C_HID_TOUCHPAD_MAX_FINGERS; i++) {
		if (event->finger[i].valid) {
			/*
			 * Windows considers any contact with width or height
			 * greater than 25mm to unintended, and expects the
			 * confidence value to be cleared for such a contact.
			 * We, however, haven't seen a touchpad that actually
			 * forwards that information to us.
			 *
			 * TODO(b/151692377): Revisit this once we have met such
			 * a device.
			 */
			touch->finger[i].confidence = 1;
			touch->finger[i].tip = 1;
			touch->finger[i].inrange = 1;
			touch->finger[i].x = event->finger[i].x;
			touch->finger[i].y = event->finger[i].y;
			touch->finger[i].width = event->finger[i].width;
			touch->finger[i].height = event->finger[i].height;
			touch->finger[i].pressure = event->finger[i].pressure;
			if (event->finger[i].is_palm)
				touch->finger[i].pressure =
						I2C_HID_TOUCHPAD_MAX_PRESSURE;
			touch->finger[i].orientation =
					event->finger[i].orientation;
			contact_num++;
		} else if (touch_old->finger[i].tip) {
			/*
			 * When the finger is leaving, we first clear the tip
			 * bit while retaining the other values. We then clear
			 * the other values at the next frame where the finger
			 * has left.
			 *
			 * Setting tip to 0 implies that the finger is leaving
			 * for both CrOS and Windows. A leaving finger would
			 * never be re-considered by the OS.
			 */

			/*
			 * First, copy old values from the previous report.
			 *
			 * This is suggested on Windows although no
			 * obvious problem has been noticed by not doing
			 * so.
			 */
			touch->finger[i] = touch_old->finger[i];

			/*
			 * Leaving finger is not a palm by definition.
			 *
			 * Not clearing the confidence bit is essential
			 * for tap-to-click to work on Windows.
			 */
			touch->finger[i].confidence = 1;

			/* Leaving finger doesn't exist. */
			touch->finger[i].tip = 0;

			/*
			 * Assume that the leaving finger is not hovering
			 * either. We would inject one single fake hovering
			 * finger later if necessary.
			 */
			touch->finger[i].inrange = 0;

			contact_num++;
		}

		/* id is like slot in Linux MT-B so it is fixed every time. */
		touch->finger[i].id = i;
	}

	/* Check for hovering activity if there is no contact report. */
	if (!contact_num) {
		if (event->hover) {
			/* Put a fake finger at slot #0 if hover is detected. */
			touch->finger[0].inrange = 1;
			touch->finger[0].x = I2C_HID_TOUCHPAD_MAX_X / 2;
			touch->finger[0].y = I2C_HID_TOUCHPAD_MAX_Y / 2;
			contact_num++;
		} else if (!touch_old->finger[0].tip &&
			   touch_old->finger[0].inrange) {
			/* Clear the fake hovering finger for host. */
			contact_num++;
		}
	}

	/* Fill in finger counts and the button state. */
	touch->count = I2C_HID_TOUCHPAD_MAX_FINGERS;
	touch->button = event->button;

	/*
	 * Windows expects scan time to be in units of 100us.  As Windows
	 * measures the delta of scan times between the first and the current
	 * report, we simply report the __hw_clock_source_read() value (which
	 * is in resolution of 1us) divided by 100 as the scan time.
	 */
	touch->timestamp = __hw_clock_source_read() / 100;

	/* Mouse report. */
	mouse->button1 = touch->button;
	if (touch->finger[0].tip == 1 && touch_old->finger[0].tip == 1) {
		/*
		 * The relative X/Y movements in the mouse report are computed
		 * based on the deltas of absolute X/Y positions between the
		 * previous and current touch report. The computed deltas need
		 * to be scaled for a smooth mouse movement.
		 */
		mouse->x = (touch->finger[0].x - touch_old->finger[0].x) /
			   I2C_HID_TOUCHPAD_MOUSE_SCALE_X;
		mouse->y = (touch->finger[0].y - touch_old->finger[0].y) /
			   I2C_HID_TOUCHPAD_MOUSE_SCALE_Y;
	} else {
		mouse->x = 0;
		mouse->y = 0;
	}

	/* Swap buffer */
	report_active_index ^= 1;
}
