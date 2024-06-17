/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/touchpad_elan.h"
#include "emul/emul_touchpad_elan.h"
#include "gpio.h"
#include "i2c.h"
#include "tablet_mode.h"
#include "test/drivers/test_state.h"
#include "update_fw.h"
#include "usb_hid_touchpad.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define TP_NODE DT_INST(0, elan_ekth3000)

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

ZTEST(touchpad_elan, test_get_info)
{
	struct touchpad_info tp_info;

	zassert_equal(touchpad_get_info(&tp_info), sizeof(tp_info));
	zassert_equal(tp_info.vendor, 0x04F3);
	zassert_equal(tp_info.elan.id, 0x2E);
	zassert_equal(tp_info.elan.fw_version, 0x03);
	zassert_equal(tp_info.elan.fw_checksum, 0xF7AC);
}

ZTEST(touchpad_elan, test_fw_update)
{
	uint8_t block[512] = {};

	zassert_ok(touchpad_update_write(0, sizeof(block), block));

	/* fail if address not aligned with page size (128b) */
	zassert_not_ok(touchpad_update_write(99, sizeof(block), block));

	/* write the last chunk to trigger finish update action */
	zassert_ok(touchpad_update_write(65536 - 512, sizeof(block), block));
	k_sleep(K_SECONDS(1));
}

__override const uint8_t allowed_command_hashes[2][32] = {
	/* sha256 of "\xAA\xAA" + "\0" * 43 */
	{ 0xc9, 0xac, 0x31, 0x22, 0xf9, 0xb0, 0xa2, 0x5a, 0x6f, 0xbb, 0x20,
	  0x07, 0xe0, 0xf3, 0xe3, 0xec, 0x5e, 0x78, 0xca, 0xee, 0x03, 0xb0,
	  0x76, 0x97, 0xcf, 0x62, 0xec, 0xf4, 0xdb, 0xaf, 0x47, 0xda },
	{},
};

ZTEST(touchpad_elan, test_debug)
{
	/* cmd1: Send (0xAA, 0xAA) to touchpad, expect 2 bytes response */
	const uint8_t cmd1[50] = { 0xff, 5, 2, 0, 2, 0xAA, 0xAA };
	/* cmd2: Retrieve data from previous command, start from byte 0 */
	const uint8_t cmd2[50] = { 0 };
	/* cmd3: Retrieve data from previous command, start from byte 64 */
	const uint8_t cmd3[50] = { 1 };
	/* cmd4: Send (0xAA, 0xAB) to touchpad */
	const uint8_t cmd4[50] = { 0xff, 5, 2, 0, 0, 0xAA, 0xAB };
	/* cmd5: Send a 60 byte cmd to touchpad */
	const uint8_t cmd5[50] = { 0xff, 5, 60, 0, 0, 0xAA, 0xAA };
	uint8_t *data;
	unsigned int data_size;

	/* send ia fake debug cmd to emulator */
	zassert_ok(touchpad_debug(cmd1, sizeof(cmd1), &data, &data_size));

	/* get the response from emulator, expect receive (0xBB, 0xBB) */
	zassert_ok(touchpad_debug(cmd2, sizeof(cmd2), &data, &data_size));
	zassert_equal(data_size, 2);
	zassert_equal(data[0], 0xBB);
	zassert_equal(data[1], 0xBB);

	zassert_equal(touchpad_debug(cmd3, sizeof(cmd3), &data, &data_size),
		      EC_RES_OVERFLOW);

	/* fail if cmd size not equal to 50 */
	zassert_equal(touchpad_debug(cmd1, 1, &data, &data_size),
		      EC_RES_UNAVAILABLE);

	/* fail if checksum of the command not in allowed_command_hashes */
	zassert_equal(touchpad_debug(cmd4, sizeof(cmd4), &data, &data_size),
		      EC_RES_INVALID_PARAM);

	/* fail if size of cmd too large */
	zassert_equal(touchpad_debug(cmd5, sizeof(cmd5), &data, &data_size),
		      EC_RES_INVALID_PARAM);
}

static void touchpad_elan_before(void *f)
{
	RESET_FAKE(set_touchpad_report);
}

ZTEST_SUITE(touchpad_elan, drivers_predicate_post_main, NULL,
	    touchpad_elan_before, NULL, NULL);
