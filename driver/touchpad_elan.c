/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "touchpad_elan.h"
#include "gpio.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_hid_touchpad.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_TOUCHPAD, outstr)
#define CPRINTF(format, args...) cprintf(CC_TOUCHPAD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_TOUCHPAD, format, ## args)

/******************************************************************************/
/* How to talk to the controller */
/******************************************************************************/

#define ETP_I2C_RESET			0x0100
#define ETP_I2C_WAKE_UP			0x0800
#define ETP_I2C_SLEEP			0x0801
#define ETP_I2C_STAND_CMD		0x0005
#define ETP_I2C_XY_TRACENUM_CMD		0x0105
#define ETP_I2C_MAX_X_AXIS_CMD		0x0106
#define ETP_I2C_MAX_Y_AXIS_CMD		0x0107
#define ETP_I2C_PRESSURE_CMD		0x010A
#define ETP_I2C_SET_CMD			0x0300

#define ETP_ENABLE_ABS		0x0001

#define ETP_I2C_REPORT_LEN		34

#define ETP_MAX_FINGERS		5
#define ETP_FINGER_DATA_LEN	5

#define ETP_PRESSURE_OFFSET	25
#define ETP_FWIDTH_REDUCE	90

#define ETP_REPORT_ID		0x5D
#define ETP_REPORT_ID_OFFSET	2
#define ETP_TOUCH_INFO_OFFSET	3
#define ETP_FINGER_DATA_OFFSET	4
#define ETP_HOVER_INFO_OFFSET	30
#define ETP_MAX_REPORT_LEN	34

struct {
	/* Max X/Y position */
	uint16_t max_x;
	uint16_t max_y;
	/* Scaling factor for finger width/height */
	uint16_t width_x;
	uint16_t width_y;
	/* Pressure adjustment */
	uint8_t pressure_adj;
} elan_tp_params;

/*
 * Report a more reasonable pressure value, so that no adjustment is necessary
 * on Chrome OS side.
 */
const int pressure_mult = 3;

static int elan_tp_read_cmd(uint16_t reg, uint16_t *val)
{
	uint8_t buf[2];
	int rv;

	buf[0] = reg;
	buf[1] = reg >> 8;

	i2c_lock(CONFIG_TOUCHPAD_I2C_PORT, 1);
	rv = i2c_xfer(CONFIG_TOUCHPAD_I2C_PORT, CONFIG_TOUCHPAD_I2C_ADDR,
		      buf, sizeof(buf), (uint8_t *)val, sizeof(*val),
		      I2C_XFER_SINGLE);
	i2c_lock(CONFIG_TOUCHPAD_I2C_PORT, 0);

	return rv;
}

static int elan_tp_write_cmd(uint16_t reg, uint16_t val)
{
	uint8_t buf[4];
	int rv;

	buf[0] = reg;
	buf[1] = reg >> 8;
	buf[2] = val;
	buf[3] = val >> 8;

	i2c_lock(CONFIG_TOUCHPAD_I2C_PORT, 1);
	rv = i2c_xfer(CONFIG_TOUCHPAD_I2C_PORT, CONFIG_TOUCHPAD_I2C_ADDR,
		      buf, sizeof(buf), NULL, 0, I2C_XFER_SINGLE);
	i2c_lock(CONFIG_TOUCHPAD_I2C_PORT, 0);

	return rv;
}

static int finger_status[ETP_MAX_FINGERS] = {0};

static int elan_tp_read_report(void)
{
	int rv;
	uint8_t tp_buf[ETP_I2C_REPORT_LEN];
	int i, ri;
	uint8_t touch_info;
	uint8_t hover_info;
	uint8_t *finger = tp_buf+ETP_FINGER_DATA_OFFSET;
	struct usb_hid_touchpad_report report;

	i2c_lock(CONFIG_TOUCHPAD_I2C_PORT, 1);
	rv = i2c_xfer(CONFIG_TOUCHPAD_I2C_PORT, CONFIG_TOUCHPAD_I2C_ADDR,
		      NULL, 0, tp_buf, ETP_I2C_REPORT_LEN, I2C_XFER_SINGLE);
	i2c_lock(CONFIG_TOUCHPAD_I2C_PORT, 0);

	if (rv) {
		CPRINTS("read report error");
		return rv;
	}

	if (tp_buf[ETP_REPORT_ID_OFFSET] != ETP_REPORT_ID) {
		CPRINTS("Invalid report id (%x)", tp_buf[ETP_REPORT_ID_OFFSET]);
		return -1;
	}

	memset(&report, 0, sizeof(report));
	report.id = 0x01;
	ri = 0; /* Next finger index in HID report */

	touch_info = tp_buf[ETP_TOUCH_INFO_OFFSET];
	hover_info = tp_buf[ETP_HOVER_INFO_OFFSET];

	if (touch_info & 0x01)
		report.button = 1;
	if (hover_info & 0x40) {
		/* TODO(crosbug.com/p/59083): Report hover event */
		CPRINTF("[TP] hover!\n");
	}

	for (i = 0; i < ETP_MAX_FINGERS; i++) {
		int valid = touch_info & (1 << (3+i));

		if (valid) {
			int width = (finger[3] & 0xf0) >> 4;
			int height = finger[3] & 0x0f;
			int pressure = finger[4] + elan_tp_params.pressure_adj;

			width = MIN(4095, width * elan_tp_params.width_x);
			height = MIN(4095, height * elan_tp_params.width_y);
			pressure = MIN(255, pressure * pressure_mult);

			report.finger[ri].tip = 1;
			report.finger[ri].inrange = 1;
			report.finger[ri].id = i;
			report.finger[ri].width = width;
			report.finger[ri].height = height;
			report.finger[ri].x =
				((finger[0] & 0xf0) << 4) | finger[1];
			report.finger[ri].y =
				elan_tp_params.max_y -
				(((finger[0] & 0x0f) << 8) | finger[2]);
			report.finger[ri].pressure = pressure;
			finger += ETP_FINGER_DATA_LEN;
			ri++;
			finger_status[i] = 1;
		} else if (finger_status[i]) {
			report.finger[ri].id = i;
			ri++;
			finger_status[i] = 0;
		}
	}

	report.count = ri;

	set_touchpad_report(&report);

	return 0;
}

/* Initialize the controller ICs after reset */
static int elan_tp_init(void)
{
	int rv;
	uint8_t val[2];

	CPRINTS("%s", __func__);

	elan_tp_write_cmd(ETP_I2C_STAND_CMD, ETP_I2C_RESET);
	msleep(100);
	i2c_lock(CONFIG_TOUCHPAD_I2C_PORT, 1);
	rv = i2c_xfer(CONFIG_TOUCHPAD_I2C_PORT, CONFIG_TOUCHPAD_I2C_ADDR,
		      NULL, 0, val, sizeof(val), I2C_XFER_SINGLE);
	i2c_lock(CONFIG_TOUCHPAD_I2C_PORT, 0);

	CPRINTS("reset rv %d buf=%04x", rv, *((uint16_t *)val));
	if (rv)
		goto out;

	/* Read min/max */
	rv = elan_tp_read_cmd(ETP_I2C_MAX_X_AXIS_CMD, &elan_tp_params.max_x);
	if (rv)
		goto out;
	rv = elan_tp_read_cmd(ETP_I2C_MAX_Y_AXIS_CMD, &elan_tp_params.max_y);
	if (rv)
		goto out;

	/* Read min/max */
	rv = elan_tp_read_cmd(ETP_I2C_XY_TRACENUM_CMD, (uint16_t *)val);
	if (rv)
		goto out;
	if (val[0] == 0 || val[1] == 0) {
		CPRINTS("Invalid XY_TRACENUM");
		goto out;
	}

	/* ETP_FWIDTH_REDUCE reduces the apparent width to avoid treating large
	 * finger as palm. Multiply value by 2 as HID multitouch divides it.
	 */
	elan_tp_params.width_x =
		2 * ((elan_tp_params.max_x / val[0]) - ETP_FWIDTH_REDUCE);
	elan_tp_params.width_y =
		2 * ((elan_tp_params.max_y / val[1]) - ETP_FWIDTH_REDUCE);

	rv = elan_tp_read_cmd(ETP_I2C_PRESSURE_CMD, (uint16_t *)val);
	if (rv)
		goto out;
	elan_tp_params.pressure_adj = (val[0] & 0x10) ? 0 : ETP_PRESSURE_OFFSET;

	CPRINTS("max=%d/%d width=%d/%d adj=%d",
		elan_tp_params.max_x, elan_tp_params.max_y,
		elan_tp_params.width_x, elan_tp_params.width_y,
		elan_tp_params.pressure_adj);

	/* Switch to absolute mode */
	rv = elan_tp_write_cmd(ETP_I2C_SET_CMD, ETP_ENABLE_ABS);
	if (rv)
		goto out;

	/* Sleep control off */
	rv = elan_tp_write_cmd(ETP_I2C_STAND_CMD, ETP_I2C_WAKE_UP);

out:
	CPRINTS("%s:%d", __func__, rv);
	return rv;
}

void elan_tp_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_TOUCHPAD);
}

void elan_tp_task(void)
{
	elan_tp_init();

	gpio_enable_interrupt(GPIO_TOUCHPAD_INT);

	while (1) {
		task_wait_event(-1);

		elan_tp_read_report();
	}
}
