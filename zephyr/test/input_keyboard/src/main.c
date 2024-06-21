/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "keyboard_scan.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;

static const struct input_kbd_matrix_common_config kbd_cfg = {
	.col_size = 88,
	.row_size = 99,
};

static const struct device *const fake_dev =
	DEVICE_DT_GET(DT_NODELABEL(fake_input_device));

DEVICE_DT_DEFINE(DT_INST(0, vnd_keyboard_input_device), NULL, NULL, NULL,
		 &kbd_cfg, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		 NULL);

FAKE_VOID_FUNC(keyboard_state_changed, int, int, int);

ZTEST(keyboard_input, test_keyboard_input_events)
{
	zassert_equal(keyboard_state_changed_fake.call_count, 0);

	input_report_abs(fake_dev, INPUT_ABS_X, 10, false, K_FOREVER);
	input_report_abs(fake_dev, INPUT_ABS_Y, 11, false, K_FOREVER);
	input_report_key(fake_dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);

	input_report_abs(fake_dev, INPUT_ABS_X, 10, false, K_FOREVER);
	input_report_abs(fake_dev, INPUT_ABS_Y, 11, false, K_FOREVER);
	input_report_key(fake_dev, INPUT_BTN_TOUCH, 0, true, K_FOREVER);

	zassert_equal(keyboard_state_changed_fake.call_count, 2);

	zassert_equal(keyboard_state_changed_fake.arg0_history[0], 11);
	zassert_equal(keyboard_state_changed_fake.arg1_history[0], 10);
	zassert_equal(keyboard_state_changed_fake.arg2_history[0], 1);

	zassert_equal(keyboard_state_changed_fake.arg0_history[1], 11);
	zassert_equal(keyboard_state_changed_fake.arg1_history[1], 10);
	zassert_equal(keyboard_state_changed_fake.arg2_history[1], 0);
}

ZTEST(keyboard_input, test_keyboard_input_enable_disable)
{
	zassert_equal(keyboard_state_changed_fake.call_count, 0);

	input_report_abs(fake_dev, INPUT_ABS_X, 1, false, K_FOREVER);
	input_report_abs(fake_dev, INPUT_ABS_Y, 2, false, K_FOREVER);
	input_report_key(fake_dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);

	zassert_equal(keyboard_state_changed_fake.call_count, 1);

	/* disable A */
	keyboard_scan_enable(0, KB_SCAN_DISABLE_A);

	input_report_key(fake_dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);

	zassert_equal(keyboard_state_changed_fake.call_count, 1);

	/* disable B */
	keyboard_scan_enable(0, KB_SCAN_DISABLE_B);

	input_report_key(fake_dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);

	zassert_equal(keyboard_state_changed_fake.call_count, 1);

	/* enable A */
	keyboard_scan_enable(1, KB_SCAN_DISABLE_A);

	input_report_key(fake_dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);

	zassert_equal(keyboard_state_changed_fake.call_count, 1);

	/* enable B */
	keyboard_scan_enable(1, KB_SCAN_DISABLE_B);

	input_report_key(fake_dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);

	zassert_equal(keyboard_state_changed_fake.call_count, 2);
}

ZTEST(keyboard_input, test_kso_gpio)
{
	const struct device *gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(DT_NODELABEL(kso_gpio), col_gpios));
	gpio_pin_t pin = DT_GPIO_PIN(DT_NODELABEL(kso_gpio), col_gpios);
	int col_num = DT_PROP(DT_NODELABEL(kso_gpio), col_num);

	zassert_equal(gpio_emul_output_get(gpio_dev, pin), 1);

	input_kbd_matrix_drive_column_hook(NULL, col_num);
	zassert_equal(gpio_emul_output_get(gpio_dev, pin), 1);

	input_kbd_matrix_drive_column_hook(fake_dev, col_num + 1);
	zassert_equal(gpio_emul_output_get(gpio_dev, pin), 0);

	input_kbd_matrix_drive_column_hook(fake_dev, col_num);
	zassert_equal(gpio_emul_output_get(gpio_dev, pin), 1);

	input_kbd_matrix_drive_column_hook(fake_dev, col_num + 1);
	zassert_equal(gpio_emul_output_get(gpio_dev, pin), 0);

	input_kbd_matrix_drive_column_hook(fake_dev,
					   INPUT_KBD_MATRIX_COLUMN_DRIVE_ALL);
	zassert_equal(gpio_emul_output_get(gpio_dev, pin), 1);

	input_kbd_matrix_drive_column_hook(fake_dev,
					   INPUT_KBD_MATRIX_COLUMN_DRIVE_NONE);
	zassert_equal(gpio_emul_output_get(gpio_dev, pin), 0);
}

uint8_t keyboard_get_cols(void);

ZTEST(keyboard_input, test_get_cols)
{
	zassert_equal(keyboard_get_cols(), 88);
}

uint8_t keyboard_get_rows(void);

ZTEST(keyboard_input, test_get_rows)
{
	zassert_equal(keyboard_get_rows(), 99);
}

extern uint8_t keyboard_cols;

ZTEST(keyboard_input, test_keyboard_cols)
{
	zassert_equal(keyboard_cols, 10);
}

#include <zephyr/shell/shell_dummy.h>

ZTEST(keyboard_input, test_ksstate)
{
	const struct shell *shell_zephyr = shell_backend_dummy_get_ptr();
	const char *outbuffer;
	size_t buffer_size;

	/* Give the backend time to initialize */
	k_sleep(K_MSEC(100));

	shell_backend_dummy_clear_output(shell_zephyr);

	zassert_ok(shell_execute_cmd(shell_zephyr, "ksstate"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(
		strstr(outbuffer, "Keyboard scan disable mask: 0x00000000"));

	shell_backend_dummy_clear_output(shell_zephyr);

	keyboard_scan_enable(0, KB_SCAN_DISABLE_A);

	zassert_ok(shell_execute_cmd(shell_zephyr, "ksstate"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(
		strstr(outbuffer, "Keyboard scan disable mask: 0x00000001"));

	keyboard_scan_enable(1, KB_SCAN_DISABLE_A);

	zassert_ok(shell_execute_cmd(shell_zephyr, "ksstate"));
	outbuffer = shell_backend_dummy_get_output(shell_zephyr, &buffer_size);
	zassert_true(buffer_size > 0, NULL);

	zassert_not_null(
		strstr(outbuffer, "Keyboard scan disable mask: 0x00000000"));
}

struct {
	int x;
	int y;
	int touch;
	int count;
} last_evt;

static void test_input_cb_handler(struct input_event *evt)
{
	if (evt->type == INPUT_EV_ABS && evt->code == INPUT_ABS_X) {
		last_evt.x = evt->value;
	} else if (evt->type == INPUT_EV_ABS && evt->code == INPUT_ABS_Y) {
		last_evt.y = evt->value;
	} else if (evt->type == INPUT_EV_KEY && evt->code == INPUT_BTN_TOUCH) {
		last_evt.touch = evt->value;
	}
	last_evt.count++;
}

INPUT_CALLBACK_DEFINE(fake_dev, test_input_cb_handler);

ZTEST(keyboard_input, test_kbpress)
{
	const struct shell *shell_zephyr = shell_backend_dummy_get_ptr();

	zassert_equal(shell_execute_cmd(shell_zephyr, "kbpress"), -EINVAL);
	zassert_equal(shell_execute_cmd(shell_zephyr, "kbpress x 2 3"),
		      -EINVAL);
	zassert_equal(shell_execute_cmd(shell_zephyr, "kbpress 1 x 3"),
		      -EINVAL);
	zassert_equal(shell_execute_cmd(shell_zephyr, "kbpress 1 2 x"),
		      -EINVAL);

	zassert_ok(shell_execute_cmd(shell_zephyr, "kbpress 3 5 1"));

	zassert_equal(last_evt.x, 3);
	zassert_equal(last_evt.y, 5);
	zassert_equal(last_evt.touch, 1);
	zassert_equal(last_evt.count, 3);
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(keyboard_state_changed);

	memset(&last_evt, 0, sizeof(last_evt));
}

ZTEST_SUITE(keyboard_input, NULL, NULL, reset, reset, NULL);
