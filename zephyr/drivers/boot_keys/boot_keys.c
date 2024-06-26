/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_boot_keys

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <dt-bindings/kbd.h>
#include <hooks.h>
#include <host_command.h>
#include <keyboard_scan.h>
#include <power_button.h>
#include <system.h>
#include <tablet_mode.h>

LOG_MODULE_REGISTER(boot_keys, LOG_LEVEL_INF);

#define CROS_EC_KEYBOARD_NODE DT_INST_PARENT(0)

/* Give the keyboard driver enough time to do a full scan and down debouncing
 * with some headroom to make sure we detect all keys pressed at boot time.
 */
#define BOOT_KEYS_SETTLE_TIME_MS \
	(DT_PROP(CROS_EC_KEYBOARD_NODE, debounce_down_ms) * 2)

static uint32_t boot_keys_value;
static uint32_t boot_keys_value_external;
static uint32_t boot_keys_counter;
static bool boot_keys_timeout;
struct k_work_delayable boot_keys_timeout_dwork;

#define BOOT_KEY_INIT(prop)                               \
	{                                                 \
		.row = KBD_RC_ROW(DT_INST_PROP(0, prop)), \
		.col = KBD_RC_COL(DT_INST_PROP(0, prop)), \
	}

static const struct boot_keys {
	uint8_t row;
	uint8_t col;
} boot_keys[] = {
	[BOOT_KEY_DOWN_ARROW] = BOOT_KEY_INIT(down_arrow_rc),
	[BOOT_KEY_ESC] = BOOT_KEY_INIT(esc_rc),
	[BOOT_KEY_LEFT_SHIFT] = BOOT_KEY_INIT(left_shift_rc),
	[BOOT_KEY_REFRESH] = BOOT_KEY_INIT(refresh_rc),
};
BUILD_ASSERT(ARRAY_SIZE(boot_keys) == BOOT_KEY_COUNT);

static bool ignore_key(uint8_t row, uint8_t col)
{
	if (!IS_ENABLED(CONFIG_BOOT_KEYS_GHOST_REFRESH_WORKAROUND)) {
		return false;
	}

	if (row == boot_keys[BOOT_KEY_REFRESH].row) {
		for (uint8_t i = 0; i < ARRAY_SIZE(boot_keys); i++) {
			const struct boot_keys *key = &boot_keys[i];

			if (row == key->row && col == key->col) {
				return false;
			}
		}
		LOG_DBG("boot_keys: ignoring row=%d col=%d", row, col);
		return true;
	}

	return false;
}

static void process_key(uint8_t row, uint8_t col, bool pressed)
{
	if (ignore_key(row, col)) {
		return;
	}

	if (pressed) {
		boot_keys_counter++;
	} else {
		boot_keys_counter--;
	}

	for (uint8_t i = 0; i < ARRAY_SIZE(boot_keys); i++) {
		const struct boot_keys *key = &boot_keys[i];

		if (row == key->row && col == key->col) {
			WRITE_BIT(boot_keys_value, i, pressed);
			break;
		}
	}

	LOG_DBG("boot_keys: boot_keys_value=0x%x counter=%d "
		"(set row=%d col=%d)",
		boot_keys_value, boot_keys_counter, row, col);
}

static void boot_keys_input_cb(struct input_event *evt)
{
	static uint8_t row;
	static uint8_t col;
	static bool pressed;

	/* Skip early once we settled and cleared all the keys */
	if (boot_keys_timeout && boot_keys_value == 0) {
		return;
	}

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
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(CROS_EC_KEYBOARD_NODE), boot_keys_input_cb);

static void power_button_change(void)
{
	/* Skip early once we settled and cleared all the keys */
	if (boot_keys_timeout && boot_keys_value == 0) {
		return;
	}

	if (power_button_is_pressed()) {
		WRITE_BIT(boot_keys_value, BOOT_KEY_POWER, 1);
		boot_keys_counter++;
	} else {
		WRITE_BIT(boot_keys_value, BOOT_KEY_POWER, 0);
		boot_keys_counter--;
	}

	LOG_DBG("boot_keys: boot_keys_value=0x%x counter=%d (power)",
		boot_keys_value, boot_keys_counter);
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, power_button_change, HOOK_PRIO_DEFAULT);

uint32_t keyboard_scan_get_boot_keys(void)
{
	return boot_keys_value_external;
}

static void boot_keys_timeout_handler(struct k_work *work)
{
	boot_keys_timeout = true;

	if (boot_keys_counter > POPCOUNT(boot_keys_value)) {
		LOG_WRN("boot_keys: stray keys, skipping");
		return;
	}

	LOG_INF("boot_keys: boot_keys_value=0x%08x", boot_keys_value);

	boot_keys_value_external = boot_keys_value;

	if (boot_keys_value & BIT(BOOT_KEY_ESC)) {
		LOG_WRN("boot_keys: recovery");
		host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);

		if (IS_ENABLED(CONFIG_TABLET_MODE)) {
			tablet_disable();
		}

		if (boot_keys_value & BIT(BOOT_KEY_LEFT_SHIFT)) {
			LOG_WRN("boot_keys: memory retraining");
			host_set_single_event(
				EC_HOST_EVENT_KEYBOARD_RECOVERY_HW_REINIT);
		}
	}
}

static void boot_keys_init(void)
{
	/* Don't check when jumping from RO to RW. */
	if (system_jumped_late()) {
		return;
	}

	/* Only check if reset is from GSC through the reset pin. */
	if ((system_get_reset_flags() & EC_RESET_FLAG_RESET_PIN) == 0) {
		return;
	}

	k_work_init_delayable(&boot_keys_timeout_dwork,
			      boot_keys_timeout_handler);
	k_work_reschedule(&boot_keys_timeout_dwork,
			  K_MSEC(BOOT_KEYS_SETTLE_TIME_MS));

	while (k_work_delayable_is_pending(&boot_keys_timeout_dwork)) {
		/* delay the rest of the boot until we finished checking for
		 * boot keys so that the host is notified before VB runs
		 */
		k_sleep(K_MSEC(1));
	}
}
DECLARE_HOOK(HOOK_INIT_EARLY, boot_keys_init, HOOK_PRIO_DEFAULT);

#if CONFIG_TEST
void test_power_button_change(void)
{
	power_button_change();
}

void test_reset(void)
{
	boot_keys_value = 0;
	boot_keys_value_external = 0;
	boot_keys_counter = 0;
	boot_keys_timeout = false;
}

void test_reinit(void)
{
	boot_keys_init();
}

bool test_dwork_pending(void)
{
	return k_work_delayable_is_pending(&boot_keys_timeout_dwork);
}
#endif
