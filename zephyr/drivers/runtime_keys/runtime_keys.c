/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_runtime_keys

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <chipset.h>
#include <dt-bindings/kbd.h>
#include <keyboard_protocol.h>
#include <system.h>

LOG_MODULE_REGISTER(runtime_keys, LOG_LEVEL_INF);

#define CROS_EC_KEYBOARD_NODE DT_INST_PARENT(0)

static uint32_t runtime_keys_counter;
static uint8_t runtime_keys_mask;

enum {
	RUNTIME_KEY_VOL_UP,
	RUNTIME_KEY_LEFT_ALT,
	RUNTIME_KEY_RIGHT_ALT,
	RUNTIME_KEY_H,
	RUNTIME_KEY_R,
};

static const uint32_t runtime_keys[] = {
	[RUNTIME_KEY_VOL_UP] = DT_INST_PROP(0, vol_up_rc),
	[RUNTIME_KEY_LEFT_ALT] = DT_INST_PROP(0, left_alt_rc),
	[RUNTIME_KEY_RIGHT_ALT] = DT_INST_PROP(0, right_alt_rc),
	[RUNTIME_KEY_H] = DT_INST_PROP(0, h_rc),
	[RUNTIME_KEY_R] = DT_INST_PROP(0, r_rc),
};

#define REBOOT_MASK_A                                          \
	(BIT(RUNTIME_KEY_VOL_UP) | BIT(RUNTIME_KEY_LEFT_ALT) | \
	 BIT(RUNTIME_KEY_R))
#define REBOOT_MASK_B                                           \
	(BIT(RUNTIME_KEY_VOL_UP) | BIT(RUNTIME_KEY_RIGHT_ALT) | \
	 BIT(RUNTIME_KEY_R))
#define HIBERNATE_MASK_A                                        \
	(BIT(RUNTIME_KEY_VOL_UP) | BIT(RUNTIME_KEY_RIGHT_ALT) | \
	 BIT(RUNTIME_KEY_H))
#define HIBERNATE_MASK_B                                       \
	(BIT(RUNTIME_KEY_VOL_UP) | BIT(RUNTIME_KEY_LEFT_ALT) | \
	 BIT(RUNTIME_KEY_H))

/* Only consider combinations of three keys (vol-up, left or right alt and a
 * letter).
 **/
#define RUNTIME_KEY_COUNT 3

static void process_key(uint8_t row, uint8_t col, bool pressed)
{
	if (pressed) {
		runtime_keys_counter++;
	} else {
		runtime_keys_counter--;
	}

	for (uint8_t i = 0; i < ARRAY_SIZE(runtime_keys); i++) {
		if (runtime_keys[i] == KBD_RC(row, col)) {
			WRITE_BIT(runtime_keys_mask, i, pressed);
		}
	}

	LOG_DBG("runtime_keys: runtime_keys_mask=0x%02x counter=%d "
		"(row=%d col=%d)",
		runtime_keys_mask, runtime_keys_counter, row, col);

	if (runtime_keys_counter != RUNTIME_KEY_COUNT) {
		return;
	}

	switch (runtime_keys_mask) {
	case REBOOT_MASK_A:
	case REBOOT_MASK_B:
		LOG_DBG("runtime_keys: reboot");
		keyboard_clear_buffer();
		chipset_reset(CHIPSET_RESET_KB_WARM_REBOOT);
		break;
	case HIBERNATE_MASK_A:
	case HIBERNATE_MASK_B:
		LOG_DBG("runtime_keys: hibernate");
		system_enter_hibernate(0, 0);
		break;
	default:
		return;
	}
}

static void runtime_keys_input_cb(struct input_event *evt)
{
	static uint8_t row;
	static uint8_t col;
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

	if (!evt->sync) {
		return;
	}

	process_key(row, col, pressed);
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(CROS_EC_KEYBOARD_NODE),
		      runtime_keys_input_cb);

#if CONFIG_TEST
void test_reinit(void)
{
	runtime_keys_counter = 0;
	runtime_keys_mask = 0;
}
#endif
