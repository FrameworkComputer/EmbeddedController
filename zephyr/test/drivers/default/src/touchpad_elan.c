/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_touchpad_elan.h"
#include "i2c.h"
#include "tablet_mode.h"
#include "test/drivers/test_state.h"
#include "usb_hid_touchpad.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define TP_NODE DT_INST(0, elan_ekth3000)

#define ETP_I2C_STAND_CMD 0x0005
#define ETP_I2C_POWER_CMD 0x0307
#define ETP_I2C_SET_CMD 0x0300

#define TP_IRQ_NODE DT_NODELABEL(gpio_touchpad_elan_int)
#define TP_IRQ_DEV DEVICE_DT_GET(DT_GPIO_CTLR(TP_IRQ_NODE, gpios))
#define TP_IRQ_PIN DT_GPIO_PIN(TP_IRQ_NODE, gpios)

#define EMUL EMUL_DT_GET(DT_NODELABEL(elan_tp_emul))

struct usb_hid_touchpad_report cached_report;

FAKE_VOID_FUNC(set_touchpad_report, struct usb_hid_touchpad_report *);
FAKE_VOID_FUNC(board_touchpad_reset);

static void cache_touchpad_report(struct usb_hid_touchpad_report *report)
{
	cached_report = *report;
}

int elan_read16(uint16_t reg, uint16_t *val)
{
	int port = I2C_PORT_BY_DEV(TP_NODE);
	int addr = DT_REG_ADDR(TP_NODE);

	return i2c_xfer(port, addr, (uint8_t *)&reg, sizeof(reg),
			(uint8_t *)val, sizeof(*val));
}

ZTEST(touchpad_elan, test_power_control)
{
	uint16_t val;

	/* tablet mode on/off implies power on/off */
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	k_sleep(K_MSEC(100));
	zassert_ok(elan_read16(ETP_I2C_POWER_CMD, &val));
	zassert_true(val & 0x01);

	tablet_set_mode(0, TABLET_TRIGGER_LID);
	k_sleep(K_MSEC(100));
	zassert_ok(elan_read16(ETP_I2C_POWER_CMD, &val));
	zassert_false(val & 0x01);

	tablet_set_mode(1, TABLET_TRIGGER_LID);
	k_sleep(K_MSEC(100));
	zassert_ok(elan_read16(ETP_I2C_POWER_CMD, &val));
	zassert_true(val & 0x01);
}

ZTEST(touchpad_elan, test_init)
{
	uint16_t val;

	/* verify that touchpad task already started and finished init */
	zassert_ok(elan_read16(ETP_I2C_SET_CMD, &val));
	zassert_true(val & 0x01);
	zassert_ok(elan_read16(ETP_I2C_STAND_CMD, &val));
	zassert_true(val & 0x0800);
}

ZTEST(touchpad_elan, test_read_report)
{
	const uint8_t raw_report[] = {
		0x22, 0x00, 0x5d, 0x08, 0x40, 0x41, 0x00, 0x14, 0x06,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0xe9,
	};

	set_touchpad_report_fake.custom_fake = cache_touchpad_report;

	touchpad_elan_emul_set_raw_report(EMUL, raw_report);

	gpio_emul_input_set(TP_IRQ_DEV, TP_IRQ_PIN, 1);
	k_sleep(K_MSEC(100));
	gpio_emul_input_set(TP_IRQ_DEV, TP_IRQ_PIN, 0);
	k_sleep(K_MSEC(100));

	zassert_equal(set_touchpad_report_fake.call_count, 1);

	zassert_equal(cached_report.finger[0].confidence, 1);
	zassert_equal(cached_report.finger[0].width, 280);
	zassert_equal(cached_report.finger[0].height, 80);
	zassert_equal(cached_report.finger[0].pressure, 19);

	zassert_equal(cached_report.finger[1].confidence, 0);
}

static void touchpad_elan_before(void *f)
{
	RESET_FAKE(set_touchpad_report);
}

ZTEST_SUITE(touchpad_elan, drivers_predicate_post_main, NULL,
	    touchpad_elan_before, NULL, NULL);
