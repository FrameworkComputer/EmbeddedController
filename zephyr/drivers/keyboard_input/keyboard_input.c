/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "keyboard_protocol.h"
#include "keyboard_scan.h"
#include "system.h"

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_kbd_matrix.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(kbd_input, CONFIG_INPUT_LOG_LEVEL);

#define CROS_EC_KEYBOARD_NODE DT_CHOSEN(cros_ec_keyboard)

static const struct device *const kbd_dev =
	DEVICE_DT_GET(CROS_EC_KEYBOARD_NODE);

static atomic_t disable_scan_mask;

uint8_t keyboard_get_cols(void)
{
	const struct input_kbd_matrix_common_config *cfg = kbd_dev->config;

	return cfg->col_size;
}

uint8_t keyboard_get_rows(void)
{
	const struct input_kbd_matrix_common_config *cfg = kbd_dev->config;

	return cfg->row_size;
}

void keyboard_scan_enable(int enable, enum kb_scan_disable_masks mask)
{
	if (enable) {
		atomic_and(&disable_scan_mask, ~mask);
	} else {
		atomic_or(&disable_scan_mask, mask);
	}
}

static void keyboard_input_cb(struct input_event *evt)
{
	static int row;
	static int col;
	static bool pressed;

	switch (evt->code) {
	case INPUT_ABS_X:
		col = evt->value;
		break;
	case INPUT_ABS_Y:
		row = evt->value;
		break;
	case INPUT_BTN_TOUCH:
		pressed = evt->value;
		break;
	}

	if (atomic_get(&disable_scan_mask) != 0) {
		return;
	}

	if (evt->sync) {
		LOG_DBG("keyboard_state_changed %d %d %d", row, col, pressed);
		keyboard_state_changed(row, col, pressed);
	}
}
INPUT_CALLBACK_DEFINE(kbd_dev, keyboard_input_cb);

/* referenced in common/keyboard_8042.c */
uint8_t keyboard_cols = DT_PROP(CROS_EC_KEYBOARD_NODE, col_size);

static int cmd_ksstate(const struct shell *sh, size_t argc, char **argv)
{
	shell_fprintf(sh, SHELL_NORMAL, "Keyboard scan disable mask: 0x%08lx\n",
		      atomic_get(&disable_scan_mask));

	return 0;
}

SHELL_CMD_REGISTER(ksstate, NULL, "Show keyboard scan state", cmd_ksstate);

static int cmd_kbpress(const struct shell *sh, size_t argc, char **argv)
{
	int err = 0;
	uint32_t row, col, val;

	col = shell_strtoul(argv[1], 0, &err);
	if (err) {
		shell_error(sh, "Invalid argument: %s", argv[1]);
		return err;
	}

	row = shell_strtoul(argv[2], 0, &err);
	if (err) {
		shell_error(sh, "Invalid argument: %s", argv[1]);
		return err;
	}

	val = shell_strtoul(argv[3], 0, &err);
	if (err) {
		shell_error(sh, "Invalid argument: %s", argv[1]);
		return err;
	}

	input_report_abs(kbd_dev, INPUT_ABS_X, col, false, K_FOREVER);
	input_report_abs(kbd_dev, INPUT_ABS_Y, row, false, K_FOREVER);
	input_report_key(kbd_dev, INPUT_BTN_TOUCH, val, true, K_FOREVER);

	return 0;
}

SHELL_CMD_ARG_REGISTER(kbpress, NULL, "Simulate keypress: ksstate col row 0|1",
		       cmd_kbpress, 4, 0);

static enum ec_status
mkbp_command_simulate_key(struct host_cmd_handler_args *args)
{
	const struct ec_params_mkbp_simulate_key *p = args->params;
	const struct input_kbd_matrix_common_config *cfg = kbd_dev->config;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	if (p->col >= cfg->col_size || p->row >= cfg->row_size)
		return EC_RES_INVALID_PARAM;

	input_report_abs(kbd_dev, INPUT_ABS_X, p->col, false, K_FOREVER);
	input_report_abs(kbd_dev, INPUT_ABS_Y, p->row, false, K_FOREVER);
	input_report_key(kbd_dev, INPUT_BTN_TOUCH, p->pressed, true, K_FOREVER);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_SIMULATE_KEY, mkbp_command_simulate_key,
		     EC_VER_MASK(0));
