/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *,
 * Dahlia inputdeck integration (I2C HID keyboard, STM32 emulating T8801).
 *
 * Deck-specific code: I2C HID dispatch, HID-to-PS/2 translation, caps-lock
 * LED via HID feature report, EC functions (Copilot, airplane mode, backlight),
 * and STM32 bootloader / firmware-flash console commands.
 */

#include "board_host_command.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "customized_shared_memory.h"
#include "gpio.h"
#include "hid_device.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "i2c_hid.h"
#include "input_deck.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_backlight.h"
#include "keyboard_protocol.h"
#include "lid_switch.h"
#include "stm32_bl.h"
#include "system.h"
#include "tablet_mode.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "watchdog.h"

#include <string.h>
#include <zephyr/drivers/gpio.h>

#define CPUTS(outstr) cputs(CC_KEYBOARD, outstr)
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_KEYBOARD, format, ##args)

/* I2C HID target configuration. */
#define HID_KBD_I2C_PORT	I2C_PORT_KB_DISCRETE
#define HID_KBD_I2C_ADDR_FLAGS	(DT_REG_ADDR(DT_NODELABEL(kb_hid)) | \
				 I2C_FLAG_ADDR16_LITTLE_ENDIAN)
#define HID_KBD_DESC_REG	I2C_HID_HID_DESC_REGISTER  /* 0x0001 */
#define HID_KBD_MAX_INPUT_BUF	128

/* HID report IDs */
#define REPORT_ID_KEYBOARD	0x01
#define REPORT_ID_CONSUMER	0x02

/* Maximum number of simultaneous keys in a HID kbd report */
/* TODO: this should be dynamic, based on the report descriptor */
#define HID_KBD_MAX_KEYS	6

/* Highest HID usage code handled (RGUI) */
#define HID_USAGEID_MAX	0xE7

#define HID_USAGEID_BREAK	0x48
/* Fake keycodes: F15 backlight, F16 copilot, F17 airplane mode */
#define HID_USAGEID_FN_SPACE	0x6A
#define HID_USAGEID_FN_RCTRL	0x6B
#define HID_USAGEID_AIRPLANE	0x6C

/* HID modifier bits */
#define HID_MOD_LCTRL		BIT(0)
#define HID_MOD_LSHIFT		BIT(1)
#define HID_MOD_LGUI		BIT(3)
#define HID_MOD_RCTRL		BIT(4)

/* Previous report state for diffing */
static uint8_t prev_mods;
static uint8_t prev_keys[HID_KBD_MAX_KEYS];

/* HID descriptor cache */
static struct i2c_hid_descriptor hid_desc;
static bool hid_initialized;
static bool hid_init_suppressed;  /* true during STM32 flash */

/* Deck GPIO specs, read from the dahlia_deck DT node's tp-en/kbd-en
 * properties. Configured as outputs by dahlia_startup.
 */
static const struct gpio_dt_spec input_deck_dahlia_tp_en =
	GPIO_DT_SPEC_GET(DT_NODELABEL(dahlia_deck), tp_en_gpios);
static const struct gpio_dt_spec input_deck_dahlia_kbd_en =
	GPIO_DT_SPEC_GET(DT_NODELABEL(dahlia_deck), kbd_en_gpios);

static bool copilot_mode;
static bool copilot_active;	/* combo currently being sent */
static bool sim_lshift;     /* we simulated Left Shift */
static bool sim_lgui;       /* we simulated Left GUI */

/* ------------------------------------------------------------------------- */

/*
 * HID usage -> PS/2 Set 2 scancode lookup, indexed by HID usage code.
 */
static const uint16_t hid_to_ps2[HID_USAGEID_MAX + 1] = {
	/* Letters */
	[0x04] = 0x001C, /* A */
	[0x05] = 0x0032, /* B */
	[0x06] = 0x0021, /* C */
	[0x07] = 0x0023, /* D */
	[0x08] = 0x0024, /* E */
	[0x09] = 0x002B, /* F */
	[0x0A] = 0x0034, /* G */
	[0x0B] = 0x0033, /* H */
	[0x0C] = 0x0043, /* I */
	[0x0D] = 0x003B, /* J */
	[0x0E] = 0x0042, /* K */
	[0x0F] = 0x004B, /* L */
	[0x10] = 0x003A, /* M */
	[0x11] = 0x0031, /* N */
	[0x12] = 0x0044, /* O */
	[0x13] = 0x004D, /* P */
	[0x14] = 0x0015, /* Q */
	[0x15] = 0x002D, /* R */
	[0x16] = 0x001B, /* S */
	[0x17] = 0x002C, /* T */
	[0x18] = 0x003C, /* U */
	[0x19] = 0x002A, /* V */
	[0x1A] = 0x001D, /* W */
	[0x1B] = 0x0022, /* X */
	[0x1C] = 0x0035, /* Y */
	[0x1D] = 0x001A, /* Z */

	/* Numbers */
	[0x1E] = 0x0016, /* 1 */
	[0x1F] = 0x001E, /* 2 */
	[0x20] = 0x0026, /* 3 */
	[0x21] = 0x0025, /* 4 */
	[0x22] = 0x002E, /* 5 */
	[0x23] = 0x0036, /* 6 */
	[0x24] = 0x003D, /* 7 */
	[0x25] = 0x003E, /* 8 */
	[0x26] = 0x0046, /* 9 */
	[0x27] = 0x0045, /* 0 */

	/* Control keys */
	[0x28] = 0x005A, /* Enter */
	[0x29] = 0x0076, /* Escape */
	[0x2A] = 0x0066, /* Backspace */
	[0x2B] = 0x000D, /* Tab */
	[0x2C] = 0x0029, /* Space */
	[0x2D] = 0x004E, /* - (minus) */
	[0x2E] = 0x0055, /* = (equals) */
	[0x2F] = 0x0054, /* [ */
	[0x30] = 0x005B, /* ] */
	[0x31] = 0x005D, /* \ (backslash) */
	[0x32] = 0x007D, /* Non-US # (International) */
	[0x33] = 0x004C, /* ; */
	[0x34] = 0x0052, /* ' (apostrophe) */
	[0x35] = 0x000E, /* ` (grave accent) */
	[0x36] = 0x0041, /* , */
	[0x37] = 0x0049, /* . */
	[0x38] = 0x004A, /* / */
	[0x39] = 0x0058, /* Caps Lock */

	/* Function keys */
	[0x3A] = 0x0005, /* F1 */
	[0x3B] = 0x0006, /* F2 */
	[0x3C] = 0x0004, /* F3 */
	[0x3D] = 0x000C, /* F4 */
	[0x3E] = 0x0003, /* F5 */
	[0x3F] = 0x000B, /* F6 */
	[0x40] = 0x0083, /* F7 */
	[0x41] = 0x000A, /* F8 */
	[0x42] = 0x0001, /* F9 */
	[0x43] = 0x0009, /* F10 */
	[0x44] = 0x0078, /* F11 */
	[0x45] = 0x0007, /* F12 */
	[0x46] = 0xE07C, /* Print screen */

	/* Special keys in the Framework layout */
	[0x47] = 0x007E, /* Scroll Lock */

	/* Navigation (extended scancodes) */
	[0x49] = 0xE070, /* Insert */
	[0x4A] = 0xE06C, /* Home */
	[0x4B] = 0xE07D, /* Page Up */
	[0x4C] = 0xE071, /* Delete */
	[0x4D] = 0xE069, /* End */
	[0x4E] = 0xE07A, /* Page Down */
	[0x4F] = 0xE074, /* Right Arrow */
	[0x50] = 0xE06B, /* Left Arrow */
	[0x51] = 0xE072, /* Down Arrow */
	[0x52] = 0xE075, /* Up Arrow */

	/* International / F13-F14 */
	[0x64] = 0x0061, /* Non-US \ (International) */
	[0x68] = 0x0064, /* F13 */
	[0x69] = 0x0067, /* F14 */

	/* Modifiers */
	[0xE0] = 0x0014, /* Left Control */
	[0xE1] = 0x0012, /* Left Shift */
	[0xE2] = 0x0011, /* Left Alt */
	[0xE3] = 0xE01F, /* Left GUI (Windows) */
	[0xE4] = 0xE014, /* Right Control */
	[0xE5] = 0x0059, /* Right Shift */
	[0xE6] = 0xE011, /* Right Alt */
};

/* ---- Caps LED (via HID feature report) ----
 * The dahlia input deck only exposes a caps-lock indicator led,
 * num and scroll lock bits sent by the OS via 8042 are dropped.
 */

/* in the 8042 LED byte */
#define CAPS_LED			BIT(2)
#define HID_LED_REPORT_ID	0x01
/* in the HID output report byte */
#define HID_LED_CAPS		BIT(1)

#define CAPS_LID_CLOSE		BIT(0)
#define CAPS_TABLET_MODE	BIT(1)
#define CAPS_SUSPEND		BIT(2)
#define CAPS_KEY_DISABLE	BIT(3)
#define CAPS_KEYBOARD_DISCONNECT BIT(4)

 /* bitmask of reasons LED should be off, default state off on boot */
static uint8_t caps_led_off = CAPS_KEY_DISABLE;

/* Forward declaration; defined with the other I2C HID helpers below. */
static int hid_i2c_write_output(uint8_t report_id, const uint8_t *data, int len);

/* ------------------------------------------------------------------------- */

static void input_deck_dahlia_led_control(void)
{
	uint8_t hid_leds = 0;

	if (!hid_initialized)
		return;

	if (!caps_led_off)
		hid_leds = HID_LED_CAPS;

	hid_i2c_write_output(HID_LED_REPORT_ID, &hid_leds, 1);
}

/* ------------------------------------------------------------------------- */

static void input_deck_dahlia_caps_set_led(unsigned int data)
{
	if (data & CAPS_LED) /* caps on */
		caps_led_off &= ~CAPS_KEY_DISABLE;
	else
		caps_led_off |= CAPS_KEY_DISABLE;

	input_deck_dahlia_led_control();
}

/*****************************************************************************
 *
 * Keyboard backlight
 * - cycleable with Fn+space
 * - stored in BBRAM to persist across reboots
 * - forced off in tablet / suspend mode
 *
 *****************************************************************************/

#ifdef CONFIG_PLATFORM_EC_PWM_KBLIGHT

/* TODO check the brightness of these values in darkness/sunlight */
static const uint8_t input_deck_dahlia_keyboard_backlight_intensity[] = { 0, 10, 40, 70, 100 };
#define KEYBOARD_BACKLIGHT_NOF_STEPS ARRAY_SIZE(input_deck_dahlia_keyboard_backlight_intensity)

/* Index into keyboard_backlight_intensity_val[]; saved/restored via BBRAM. */
static uint8_t input_deck_dahlia_keyboard_backlight_intensity_index;


/* if keyboard backlight is forced off - "normal" off is set by intensity 0 */

static void input_deck_dahlia_keyboard_backlight_set_brightness(bool on)
{
	uint8_t val = on ? input_deck_dahlia_keyboard_backlight_intensity[
		input_deck_dahlia_keyboard_backlight_intensity_index]:0;

	kblight_set(val);
}

/* ------------------------------------------------------------------------- */

static void input_deck_dahlia_keyboard_backlight_cycle(void)
{
	input_deck_dahlia_keyboard_backlight_intensity_index =
		(input_deck_dahlia_keyboard_backlight_intensity_index + 1u)
		% KEYBOARD_BACKLIGHT_NOF_STEPS;
	/* is there any condition when the backlight can be cycled while off? */
	input_deck_dahlia_keyboard_backlight_set_brightness(true);
}
#else
#warning keyboard backlight disabled
static void input_deck_dahlia_keyboard_backlight_set_brightness(bool on) { }
static void input_deck_dahlia_keyboard_backlight_cycle(void) { }
#endif /* CONFIG_PLATFORM_EC_PWM_KBLIGHT */

/* ------------------------------------------------------------------------ */

/* disable keyboard/tp when closed or tablet mode */
static void input_deck_dahlia_enable_by_mode(void)
{
	bool enable = !tablet_get_mode() && lid_is_open();

	gpio_pin_set_dt(&input_deck_dahlia_tp_en,  enable);
	gpio_pin_set_dt(&input_deck_dahlia_kbd_en, enable);

	/* turn off backlight if tablet or closed or suspended */
	input_deck_dahlia_keyboard_backlight_set_brightness(
		enable && !chipset_in_state(CHIPSET_STATE_ANY_SUSPEND));
}

/* ------------------------------------------------------------------------- */

/* Tablet/lid hook is invoked via input_deck.c's dispatcher (on the ops). */
static void input_deck_dahlia_tablet_or_lid_change(void)
{
	input_deck_dahlia_enable_by_mode();

	/* Caps LED state mirrors lid/tablet too. */
	if (lid_is_open())
		caps_led_off &= ~CAPS_LID_CLOSE;
	else
		caps_led_off |= CAPS_LID_CLOSE;

	if (tablet_get_mode())
		caps_led_off |= CAPS_TABLET_MODE;
	else
		caps_led_off &= ~CAPS_TABLET_MODE;

	input_deck_dahlia_led_control();
}

/* ------------------------------------------------------------------------- */

/* Save settings as one byte to BBRAM:
 *   bit 7   : Fn Lock (reserved for future use)
 *   bit 6   : Copilot mode
 *   bits 0-2: keyboard backlight step (0-4)
 * Shared across laptop12 mainboards that host the dahlia deck.
 */
#define BBRAM_COPILOT_MODE	BIT(6)
#define BBRAM_KBL_MASK		0x07

static void input_deck_dahlia_save_settings(void)
{
	uint8_t current_kb = 0;

	if (copilot_mode)
		current_kb |= BBRAM_COPILOT_MODE;

	current_kb |= (input_deck_dahlia_keyboard_backlight_intensity_index & BBRAM_KBL_MASK);
	system_set_bbram(SYSTEM_BBRAM_IDX_KBSTATE, current_kb);
}

/* ------------------------------------------------------------------------- */

static void input_deck_dahlia_load_settings(void)
{
	uint8_t current_kb = 0;

	if (system_get_bbram(SYSTEM_BBRAM_IDX_KBSTATE, &current_kb) != EC_SUCCESS)
		return;

	copilot_mode = (bool)(current_kb & BBRAM_COPILOT_MODE);

	input_deck_dahlia_keyboard_backlight_intensity_index = current_kb & BBRAM_KBL_MASK;
	if (input_deck_dahlia_keyboard_backlight_intensity_index >= KEYBOARD_BACKLIGHT_NOF_STEPS)
		input_deck_dahlia_keyboard_backlight_intensity_index
			= KEYBOARD_BACKLIGHT_NOF_STEPS - 1;

	CPRINTS("KB: copilot mode %s, backlight step %d",
		copilot_mode ? "on" : "off", input_deck_dahlia_keyboard_backlight_intensity_index);
}

/* ------------------------------------------------------------------------- */

/*
 * Send a HID output report to the target (for keyboard LED state).
 * On the wire via wOutputRegister:
 *   [reg_lo, reg_hi, length_lo, length_hi, report_id, data...]
 * where length includes the 2-byte length field itself + report id + data.
 */
static int hid_i2c_write_output(uint8_t report_id, const uint8_t *data, int len)
{
	uint8_t buf[8];
	uint16_t total_len = 3 + len; /* length(2) + id(1) + data(len) */

	if (total_len + 2 > sizeof(buf))
		return EC_ERROR_INVAL;

	buf[0] = hid_desc.wOutputRegister & 0xFF;
	buf[1] = (hid_desc.wOutputRegister >> 8) & 0xFF;
	buf[2] = total_len & 0xFF;
	buf[3] = (total_len >> 8) & 0xFF;
	buf[4] = report_id;
	memcpy(buf + 5, data, len);

	return i2c_xfer(HID_KBD_I2C_PORT, HID_KBD_I2C_ADDR_FLAGS,
			buf, 5 + len, NULL, 0);
}

/* ------------------------------------------------------------------------- */

/* HACK! Read BIOS setting for Fn/Ctrl swap from the sunflower matrix */
/* Use vendor extension to send swap command */

#define HID_VENDOR_REPORT_ID		0x20
#define HID_VENDOR_PAYLOAD_SIZE		31
#define HID_VENDOR_OP_SET_SWAP_FN_LCTRL	0x10
/* HID command byte: Feature report, ID >= 15 follows as its own byte */
#define HID_VENDOR_CMD_FEATURE		0x3F

/* Send a vendor command: a HID SET_REPORT (host writes a report to the
 * device), as two writes — command frame, then the report payload.
 */
static int hid_vendor_set(const uint8_t *req, int req_len)
{
	uint8_t buf[2 + 2 + 1 + HID_VENDOR_PAYLOAD_SIZE];
	uint16_t length = 2 + 1 + HID_VENDOR_PAYLOAD_SIZE;
	int ret;

	buf[0] = hid_desc.wCommandRegister & 0xFF;
	buf[1] = (hid_desc.wCommandRegister >> 8) & 0xFF;
	buf[2] = HID_VENDOR_CMD_FEATURE;
	buf[3] = I2C_HID_CMD_SET_REPORT;
	buf[4] = HID_VENDOR_REPORT_ID;
	buf[5] = hid_desc.wDataRegister & 0xFF;
	buf[6] = (hid_desc.wDataRegister >> 8) & 0xFF;

	ret = i2c_xfer(HID_KBD_I2C_PORT, HID_KBD_I2C_ADDR_FLAGS, buf, 7, NULL, 0);
	if (ret)
		return ret;

	crec_msleep(2);

	if (req_len > HID_VENDOR_PAYLOAD_SIZE)
		req_len = HID_VENDOR_PAYLOAD_SIZE;

	buf[0] = hid_desc.wDataRegister & 0xFF;
	buf[1] = (hid_desc.wDataRegister >> 8) & 0xFF;
	buf[2] = length & 0xFF;
	buf[3] = (length >> 8) & 0xFF;
	buf[4] = HID_VENDOR_REPORT_ID;
	memcpy(buf + 5, req, req_len);
	memset(buf + 5 + req_len, 0, HID_VENDOR_PAYLOAD_SIZE - req_len);

	return i2c_xfer(HID_KBD_I2C_PORT, HID_KBD_I2C_ADDR_FLAGS, buf, sizeof(buf), NULL, 0);
}

/* ------------------------------------------------------------------------- */

static void input_deck_dahlia_sync_fn_ctrl_swap(void)
{
	/* Swapped if BIOS put Fn in LCtrl's matrix cell. */
	uint8_t req[2] = {HID_VENDOR_OP_SET_SWAP_FN_LCTRL, get_scancode_set2(1, 14) == SCANCODE_FN};

	CPRINTS("HID kbd: fn/ctrl %s (cell(1,14)=0x%04x cell(0,16)=0x%04x)",
		req[1] ? "swapped" : "not swapped",
		get_scancode_set2(1, 14), get_scancode_set2(0, 16));

	if (hid_vendor_set(req, sizeof(req)) != EC_SUCCESS)
		CPRINTS("HID kbd: fn/ctrl swap sync failed");
}

/* ------------------------------------------------------------------------- */
/*
 * Inject a HID usage code via simulate_keyboard(). No real or virtual key matrix.
 */
static void input_deck_dahlia_inject_key(uint8_t hid_usage, uint8_t modifiers, bool pressed)
{
	if (hid_usage > HID_USAGEID_MAX)
		return;

	if (pressed && hid_usage == HID_USAGEID_BREAK) {
		/* BREAK is sent as Ctrl-PAUSE */
		if (modifiers & (HID_MOD_LCTRL|HID_MOD_RCTRL)) {
			simulate_keyboard(0xe07e, 1);
			simulate_keyboard(0xe0, 1);
			simulate_keyboard(0x7e, 0);
		} else {
			simulate_keyboard(0xe114, 1);
			simulate_keyboard(0x77, 1);
			simulate_keyboard(0xe1, 1);
			simulate_keyboard(0x14, 0);
			simulate_keyboard(0x77, 0);
		}
		return;
	}

	uint16_t ps2 = hid_to_ps2[hid_usage];

	if (ps2 == 0)
		return;

	simulate_keyboard(ps2, pressed);

	/* is this needed? sunflower don't use this */
	#ifdef DISABLED_CODE /* bypass linter */
	if (pressed)
		host_set_single_event(EC_HOST_EVENT_KEY_PRESSED);
	#endif
}

/* ------------------------------------------------------------------------- */

/* common hack to check if we are pre-OS */
static bool is_bios_mode(void)
{
	return !(*(host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS)) & BIT(0));
}

/* ------------------------------------------------------------------------- */

/* helper to be able to find differences of pressed keys between events */
static bool is_key_in_array(uint8_t usage, const uint8_t *arr, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (arr[i] == usage)
			return true;
	}
	return false;
}

/* ------------------------------------------------------------------------- */

/*
 * Handle Copilot mode: when active, Right Ctrl becomes Win+Shift+F23.
 * Only simulates modifiers that aren't already physically held.
 */
static void copilot_press(uint8_t mods)
{
	if (copilot_active)
		return;

	copilot_active = true;

	if (!(mods & HID_MOD_LGUI)) {
		simulate_keyboard(SCANCODE_LEFT_WIN, 1);
		sim_lgui = true;
	}

	if (!(mods & HID_MOD_LSHIFT)) {
		simulate_keyboard(SCANCODE_LEFT_SHIFT, 1);
		sim_lshift = true;
	}

	simulate_keyboard(SCANCODE_F23, 1);
}

/* ------------------------------------------------------------------------- */

static void copilot_release(void)
{
	if (!copilot_active)
		return;

	copilot_active = false;

	simulate_keyboard(SCANCODE_F23, 0);

	if (sim_lshift) {
		simulate_keyboard(SCANCODE_LEFT_SHIFT, 0);
		sim_lshift = false;
	}

	if (sim_lgui) {
		simulate_keyboard(SCANCODE_LEFT_WIN, 0);
		sim_lgui = false;
	}
}

/* ------------------------------------------------------------------------- */

/*
 * Process a keyboard HID report (Report ID 0x01).
 * Format: [modifiers, reserved, key1, key2, key3, key4, key5, key6]
 *
 * Diffs against the previous report and injects press/release events
 * directly into the 8042 scancode path.
 */
#define HID_USAGEID_P 0x13u
#define HID_USAGEID_F9 0x42u
#define HID_USAGEID_F10 0x43u
#define HID_USAGEID_F11 0x44u
#define HID_USAGEID_PRTSCR 0x46u

static void input_deck_dahlia_process_keyboard_report(uint8_t *const data, size_t len)
{
	uint8_t mods = data[0];
	/* data[1] is reserved */
	uint8_t *keys = data + 2;
	unsigned int num_keys = len > HID_KBD_MAX_KEYS+2u ? len-2u : HID_KBD_MAX_KEYS;

	if (is_bios_mode()) {
		/* map display/airplane mode to F9/F10/F11 for BIOS */
		for (int i = 0; i < num_keys; i++) {
			if (keys[i] == HID_USAGEID_P && (mods & HID_MOD_LGUI)) {
				keys[i] = HID_USAGEID_F9;
				mods &= ~HID_MOD_LGUI;
			} else if (keys[i] == HID_USAGEID_AIRPLANE) {
				keys[i] = HID_USAGEID_F10;
			} else if (keys[i] == HID_USAGEID_PRTSCR) {
				keys[i] = HID_USAGEID_F11;
			}
		}
	}

	/* Josh hack to enable/disable copilot key */
	copilot_mode = (get_scancode_set2(3, 14) == SCANCODE_FAKE_COPILOT);

	/* Check for EC-handled Fn-layer keys */
	for (unsigned int i = 0; i < num_keys; i++) {
		if (keys[i] == HID_USAGEID_AIRPLANE) {
			hid_airplane(true);
			return; /* consume entire report */
		}
		if (keys[i] == HID_USAGEID_FN_SPACE) {
			input_deck_dahlia_keyboard_backlight_cycle();
			return; /* consume entire report */
		}
	}

	/* Handle Copilot combo for Right Ctrl */
	if (copilot_mode) {
		if (mods & HID_MOD_RCTRL) {
			/* Remove Right Ctrl from modifiers before injection */
			mods &= ~HID_MOD_RCTRL;
			copilot_press(mods);
		} else
			copilot_release();
	}

	/* Release keys no longer present (before releasing modifiers) */
	for (unsigned int i = 0; i < HID_KBD_MAX_KEYS; i++) {
		if (prev_keys[i] && !is_key_in_array(prev_keys[i], keys, num_keys))
			input_deck_dahlia_inject_key(prev_keys[i], mods, false);
	}

	/* bits of new and old bits */
	uint8_t mod_diff = prev_mods ^ mods;

	/* Inject 1 for freshly pressed modifiers, a 0 for released */
	for (unsigned int i = 0; i < 8; i++) {
		if (mod_diff & BIT(i))
			input_deck_dahlia_inject_key(0xE0 + i, mods, (bool)(mods & BIT(i)));
	}

	/* Press newly present keys (skip EC-internal usage codes) */
	for (int i = 0; i < num_keys; i++) {
		if (keys[i] &&
				keys[i] != HID_USAGEID_FN_SPACE &&
				keys[i] != HID_USAGEID_AIRPLANE &&
				!is_key_in_array(keys[i], prev_keys, HID_KBD_MAX_KEYS))
			input_deck_dahlia_inject_key(keys[i], mods, true);
	}

	/* Save state for next diff */
	prev_mods = mods;
	memset(prev_keys, 0, sizeof(prev_keys));
	memcpy(prev_keys, keys, MIN(num_keys, HID_KBD_MAX_KEYS));
}

/* ------------------------------------------------------------------------- */

/* HID Consumer page 0x0C usage IDs */
#define HID_USAGEID_MUTE 0x00E2u
#define HID_USAGEID_VOLUME_DECREASE 0x00EAu
#define HID_USAGEID_VOLUME_INCREASE 0x00E9u
#define HID_USAGEID_SCAN_TRACK_FORWARD 0x00B5u
#define HID_USAGEID_SCAN_TRACK_BACKWARD 0x00B6u
#define HID_USAGEID_PAUSE_PLAY 0x00CDu
/* Brightness (0x006F/0x0070) are consumer reports for Win11 */
#define HID_USAGEID_BRIGHTNESS_DECREASE 0x006fu
#define HID_USAGEID_BRIGHTNESS_INCREASE 0x0070u
/* consumer control configuration, mapped to non-fn F12 */
#define HID_USAGEID_AL_CCC 0x0183u

/*
 * Translate HID Consumer Page usage codes to PS/2 scancodes.
 * Returns 0 if no PS/2 equivalent — fall back to hid_consumer().
 */
static uint16_t input_deck_dahlia_consumer_usage_to_scancode(uint16_t usage)
{
	/* note: this uses PS2 scancodes, not HID */
	if (!is_bios_mode()) {
		switch (usage) {
		case HID_USAGEID_MUTE:                return SCANCODE_VOLUME_MUTE;
		case HID_USAGEID_VOLUME_DECREASE:     return SCANCODE_VOLUME_DOWN;
		case HID_USAGEID_VOLUME_INCREASE:     return SCANCODE_VOLUME_UP;
		case HID_USAGEID_SCAN_TRACK_FORWARD:  return SCANCODE_NEXT_TRACK;
		case HID_USAGEID_SCAN_TRACK_BACKWARD: return SCANCODE_PREV_TRACK;
		case HID_USAGEID_PAUSE_PLAY:          return 0xe034; /* Play/Pause */
		default: return 0;
		}
	} else {
		/* map media keys pre OS to their function key equivalents */
		switch (usage) {
		case HID_USAGEID_MUTE:                return SCANCODE_F1;
		case HID_USAGEID_VOLUME_DECREASE:     return SCANCODE_F2;
		case HID_USAGEID_VOLUME_INCREASE:     return SCANCODE_F3;
		case HID_USAGEID_SCAN_TRACK_BACKWARD: return SCANCODE_F4;
		case HID_USAGEID_PAUSE_PLAY:          return SCANCODE_F5;
		case HID_USAGEID_SCAN_TRACK_FORWARD:  return SCANCODE_F6;
		case HID_USAGEID_BRIGHTNESS_DECREASE: return SCANCODE_F7;
		case HID_USAGEID_BRIGHTNESS_INCREASE: return SCANCODE_F8;
		/* F9, F10, F11 are HID keycodes and processed elsewhere */
		case HID_USAGEID_AL_CCC:              return SCANCODE_F12;
		default: return 0;
		}
	}
}

/* ------------------------------------------------------------------------- */

/*
 * Process a consumer HID report (Report ID 0x02).
 * Format: [usage_lo, usage_hi] -- 16-bit LE consumer usage code.
 */
static void input_deck_dahlia_process_consumer_report(const uint8_t *data, int len)
{
	static uint16_t last_consumer_scancode;

	if (len < 2)
		return;

	uint16_t usage = (uint16_t)data[0] | ((uint16_t)data[1] << 8);

	if (usage != 0) {
		uint16_t sc = input_deck_dahlia_consumer_usage_to_scancode(usage);

		if (sc) {
			simulate_keyboard(sc, 1);
			last_consumer_scancode = sc;
		} else {
			/* No PS/2 equivalent — use HID consumer path */
			hid_consumer(usage, true);
			last_consumer_scancode = 0;
		}
	} else {
		if (last_consumer_scancode) {
			simulate_keyboard(last_consumer_scancode, 0);
			last_consumer_scancode = 0;
		} else {
			hid_consumer(0, false);
		}
	}
}

/* ------------------------------------------------------------------------- */

/*
 * Process a raw input report buffer from the I2C HID target.
 * Format: [length_lo, length_hi, report_id, ...payload]
 */
static void i2c_hid_process_input_report(uint8_t *buf, unsigned int max_len)
{
	uint16_t length = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);

	/* Length 0 happens during init */
	if (!hid_initialized && !length) {
		CPRINTS("HID kbd: reset complete");
		return;
	}

	if (length < 3 || length > max_len) {
		CPRINTS("HID kbd: bad report len %u", length);
		return;
	}

	uint8_t report_id = buf[2];
	uint8_t *report_data = buf + 3;
	unsigned int report_len = length - 3;

	switch (report_id) {
	case REPORT_ID_KEYBOARD:
		if (report_data[0] || (report_len > 2 && report_data[2])) {
			char dbg[20] = { 0 };
			char *s = dbg;
			unsigned int n = report_len < 8 ? report_len : 8;

			for (int i = 2; i < n; i++)
				s += sprintf(s, "%02X ", report_data[i]);

			CPRINTS("HID kbd: key rpt mods=0x%02X keys=[%s]", report_data[0], dbg);
		}
		input_deck_dahlia_process_keyboard_report(report_data, report_len);
		break;
	case REPORT_ID_CONSUMER:
		CPRINTS("HID kbd: consumer rpt usage=0x%04X",
			report_len >= 2 ?
			((uint16_t)report_data[0] | ((uint16_t)report_data[1] << 8)) : 0);
		input_deck_dahlia_process_consumer_report(report_data, report_len);
		break;
	default:
		CPRINTS("HID kbd: unknown report ID 0x%02X len=%d", report_id, report_len);
		break;
	}
}

/* ------------------------------------------------------------------------- */

/* Plain read (no register address) -- used for input reports. */
static inline int hid_i2c_read_input(uint8_t *buf, int len)
{
	return i2c_xfer(HID_KBD_I2C_PORT, HID_KBD_I2C_ADDR_FLAGS, NULL, 0, buf, len);
}

/* ------------------------------------------------------------------------- */

/* this func reschedule itself on fails, so it needs to see its deferrable */
static void i2c_hid_kbd_init_deferred(void);
DECLARE_DEFERRED(i2c_hid_kbd_init_deferred);

/* ------------------------------------------------------------------------- */

/* TODO: reduce the debug code here, takes space and all */

/* I2C HID initialization */
static void i2c_hid_kbd_init_deferred(void)
{
	uint8_t buf[HID_KBD_MAX_INPUT_BUF];

	if (hid_init_suppressed) /* flashing in progress */
		return;

	CPRINTS("HID kbd: init start, port=%d", HID_KBD_I2C_PORT);

	/* Read HID descriptor from register 0x0001 */
	int ret = i2c_read_offset16_block(HID_KBD_I2C_PORT, HID_KBD_I2C_ADDR_FLAGS,
			HID_KBD_DESC_REG, (uint8_t *)&hid_desc, sizeof(hid_desc));

	if (ret) {
		int irq_pin = gpio_get_level(GPIO_KB_DISCRETE_INT);

		CPRINTS("HID kbd: desc read err %d, IRQ pin=%d", ret, irq_pin);
		hook_call_deferred(&i2c_hid_kbd_init_deferred_data, 10000*MSEC);
		return;
	}

	/* Validate descriptor */
	if (hid_desc.wHIDDescLength != I2C_HID_DESC_LENGTH ||
	    hid_desc.bcdVersion != I2C_HID_BCD_VERSION) {
		CPRINTS("HID kbd: bad desc (len=%u ver=0x%04X)",
			hid_desc.wHIDDescLength, hid_desc.bcdVersion);
		hook_call_deferred(&i2c_hid_kbd_init_deferred_data, 10000*MSEC);
		return;
	}

	CPRINTS("HID kbd: desc OK (input=%u report_desc=%u)",
		hid_desc.wMaxInputLength, hid_desc.wReportDescLength);

	if (hid_desc.wVendorID == 0x32AC && hid_desc.wProductID == 0x0038) {
		/* wVersionID is nibble-encoded: 0x1234 = 12.3.4 */
		CPRINTS("HID kbd: detected Dahlia input module, fw version %x.%x.%x",
			hid_desc.wVersionID >> 8, (hid_desc.wVersionID >> 4) & 0xf,
			hid_desc.wVersionID & 0xf);
	} else {
		CPRINTS("HID kbd: unknown VID=0x%04X PID=0x%04X ver=0x%04X",
			hid_desc.wVendorID, hid_desc.wProductID,
			hid_desc.wVersionID);
	}

	/* Read report descriptor (not parsed yet) */
	if (hid_desc.wReportDescLength > 0 && hid_desc.wReportDescLength <= sizeof(buf)) {
		ret = i2c_read_offset16_block(HID_KBD_I2C_PORT,
				HID_KBD_I2C_ADDR_FLAGS,
				hid_desc.wReportDescRegister,
				buf, hid_desc.wReportDescLength);
		if (ret)
			CPRINTS("HID kbd: report desc read err %d", ret);
	}

	/* Read any pending input reports */
	if (hid_desc.wMaxInputLength <= sizeof(buf)) {
		ret = hid_i2c_read_input(buf, hid_desc.wMaxInputLength);

		if (ret == 0)
			i2c_hid_process_input_report(buf, hid_desc.wMaxInputLength);
	}

	/* Send RESET command */
	uint8_t cmd[2] = { 0x00, I2C_HID_CMD_RESET };

	ret = i2c_write_offset16_block(HID_KBD_I2C_PORT, HID_KBD_I2C_ADDR_FLAGS,
			hid_desc.wCommandRegister, cmd, sizeof(cmd));

	if (ret)
		CPRINTS("HID kbd: reset err %d", ret);

	/* Wait for reset sentinel (length=0 report) */
	crec_msleep(100);

	if (hid_desc.wMaxInputLength <= sizeof(buf)) {
		ret = hid_i2c_read_input(buf, hid_desc.wMaxInputLength);
		if (ret == 0)
			i2c_hid_process_input_report(buf, hid_desc.wMaxInputLength);
	}

	hid_initialized = true;

	/* HID is up now — caps_led_control() can finally drive the LED. */
	input_deck_dahlia_led_control();

	input_deck_dahlia_sync_fn_ctrl_swap();

	CPRINTS("HID kbd: init complete");
}

/* ------------------------------------------------------------------------- */

/* Deferred interrupt work: reads the input report, scheduled from IRQ handler */
static void i2c_hid_kbd_deferred(void)
{
	uint8_t buf[HID_KBD_MAX_INPUT_BUF];

	if (!hid_initialized)
		return;

	int read_len = MIN(hid_desc.wMaxInputLength, sizeof(buf));
	int ret = hid_i2c_read_input(buf, read_len);

	if (ret) {
		CPRINTS("HID kbd: read err %d", ret);
		return;
	}

	i2c_hid_process_input_report(buf, read_len);
}
DECLARE_DEFERRED(i2c_hid_kbd_deferred);

/* ------------------------------------------------------------------------- */

/* IRQ handler — invoked from input_deck.c */
static void input_deck_dahlia_kb_irq(void)
{
	/* schedule the actual i2c read */
	hook_call_deferred(&i2c_hid_kbd_deferred_data, 0);
}

/* ------------------------------------------------------------------------- */

static void input_deck_dahlia_power(bool on)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_3v_tp), on);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_5v_tp), on);
	crec_msleep(200); /* TODO tweak this - it is a safe delay for now */
}

/* ------------------------------------------------------------------------- */

/* Power up the dahlia deck. STM32 samples BOOT0 at reset; if high
 * it boots into bootloader mode.
 */
static void input_deck_dahlia_power_on(void)
{
	gpio_set_flags(GPIO_KB_DISCRETE_INT, GPIO_OUTPUT | GPIO_OUT_LOW);
	input_deck_dahlia_power(true);

	gpio_set_flags(GPIO_KB_DISCRETE_INT, GPIO_INPUT | GPIO_PULL_UP);

	input_deck_dahlia_load_settings();
}

/* ------------------------------------------------------------------------- */

static void input_deck_dahlia_power_off(void)
{
	/* Cancel the HID keyboard init retry */
	hook_call_deferred(&i2c_hid_kbd_init_deferred_data, -1);
	input_deck_dahlia_power(false);
}

/* ------------------------------------------------------------------------- */

/* Called by input_deck.c via ops table at the end */
static void input_deck_dahlia_resume(void)
{
	/* Configure deck pins as outputs and apply initial state.
	 * The (emulated) IT8801 gpios should be available.
	 */

	/* Note that the it8801 driver keeps cached values of pin states,
	 * and updates physical pins only when it deems necessary.
	 * Muxing them with default high aligns the default cached values (0)
	 * with default physical pins (1).
	 */
	gpio_pin_configure_dt(&input_deck_dahlia_tp_en, GPIO_OUTPUT_HIGH);
	gpio_pin_configure_dt(&input_deck_dahlia_kbd_en, GPIO_OUTPUT_HIGH);

	input_deck_dahlia_enable_by_mode();

	/* HID side: kick off descriptor read; enable IRQ on the deck pin. */
	hook_call_deferred(&i2c_hid_kbd_init_deferred_data, 25 * MSEC);
	gpio_enable_interrupt(GPIO_KB_DISCRETE_INT);

	/* The chip is reachable now — drop the "deck not ready" bit so the
	 * caps LED can actually be driven.
	 */
	caps_led_off &= ~(CAPS_SUSPEND | CAPS_KEYBOARD_DISCONNECT);
	input_deck_dahlia_led_control();
}

/* ------------------------------------------------------------------------- */

static void input_deck_dahlia_suspend(void)
{
	caps_led_off |= CAPS_SUSPEND | CAPS_KEYBOARD_DISCONNECT;
	input_deck_dahlia_led_control();

	input_deck_dahlia_enable_by_mode();
	input_deck_dahlia_keyboard_backlight_set_brightness(false);
	input_deck_dahlia_save_settings();
}

/* ------------------------------------------------------------------------- */


#ifdef CONFIG_PLATFORM_EC_STM32_KEYBOARD
/*****************************************************************************
 *
 * STM32 programming commands
 * - enter bootloader
 * - reset
 * - update firmware
 *
 *****************************************************************************/

/* Let STM32 enter bootloader mode: BOOT0 pulled high when powered on */
static void kbd_stm32_bootloader_enter_sequence(void)
{
	hid_initialized = false;
	hid_init_suppressed = true;
	hook_call_deferred(&i2c_hid_kbd_init_deferred_data, -1);

	input_deck_dahlia_power(false);

	gpio_set_flags(GPIO_KB_DISCRETE_INT, GPIO_OUTPUT | GPIO_OUT_HIGH);
	CPRINTS("KB: BOOT0 driven high, pin reads %d", gpio_get_level(GPIO_KB_DISCRETE_INT));
	crec_msleep(100);

	input_deck_dahlia_power(true);

	gpio_set_flags(GPIO_KB_DISCRETE_INT, GPIO_INPUT | GPIO_PULL_UP);
}

/* ------------------------------------------------------------------------- */

/* Bring the STM32 back to keyboard mode and enable the HID scanner */
static void kbd_stm32_bootloader_exit_sequence(void)
{
	hid_initialized = false;
	hid_init_suppressed = false;

	input_deck_dahlia_power(false);

	gpio_set_flags(GPIO_KB_DISCRETE_INT, GPIO_OUTPUT | GPIO_OUT_LOW);
	CPRINTS("KB: BOOT0 driven low, pin reads %d", gpio_get_level(GPIO_KB_DISCRETE_INT));
	crec_msleep(100);

	input_deck_dahlia_power(true);

	gpio_set_flags(GPIO_KB_DISCRETE_INT, GPIO_INPUT | GPIO_PULL_UP);

	hook_call_deferred(&i2c_hid_kbd_init_deferred_data, 500 * MSEC);
}

/* ------------------------------------------------------------------------- */

/* EC kbboot command, should perhaps be moved outside the STM32 flash code? */
static int cmd_kbboot(int argc, const char **argv)
{
	CPRINTS("KB: entering bootloader mode");
	kbd_stm32_bootloader_enter_sequence();
	CPRINTS("KB: deck powered, STM32 should be in bootloader");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kbboot, cmd_kbboot, NULL,
	"Enter STM32 keyboard controller bootloader mode");

/* ------------------------------------------------------------------------- */

/* EC kbreset, same location as kbboot? */
static int cmd_kbreset(int argc, const char **argv)
{
	CPRINTS("KB: resetting keyboard controller");
	kbd_stm32_bootloader_exit_sequence();
	CPRINTS("KB: deck powered, STM32 should be in normal mode");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kbreset, cmd_kbreset, NULL,
	"Reset STM32 keyboard controller into normal mode");

/* ------------------------------------------------------------------------- */

/* STM32 flash commands */

/* What is below is an adaptation of stmflash-0.7 by Claude Opus 4.7 */

#define KBFLASH_CHUNK_TIMEOUT_US	(10 * SECOND)
#define KBFLASH_CHUNK_SIZE		256
#define KBFLASH_UART_ACK		0x06
#define KBFLASH_UART_NAK		0x15

/* Static state for the deferred flash worker */
static struct {
	struct stm32_bl_dev dev;
	uint32_t base_addr;
	uint16_t bl_addr;
	int fw_size;
	int do_verify;
} kbflash_state;

/* ------------------------------------------------------------------------- */

/* Read exactly 'len' bytes from UART to buf. Returns bytes read. */
static int kbflash_uart_read(uint8_t *buf, int len)
{
	timestamp_t deadline = get_time();
	int i;

	deadline.val += KBFLASH_CHUNK_TIMEOUT_US;

	for (i = 0; i < len; i++) {
		int c;

		while ((c = uart_getc()) < 0) {
			if (get_time().val >= deadline.val)
				return i;
			watchdog_reload();
		}
		buf[i] = (uint8_t)c;
	}

	return len;
}

/*
 * Deferred flash worker — runs in the hooks task, NOT the shell thread.
 * This is critical: uart_shell_rx_bypass() relies on the shell thread
 * being free to call bypass_cb, which fills rx_buffer for uart_getc().
 */

/* ------------------------------------------------------------------------- */

static void kbflash_deferred(void)
{
	int total_chunks, chunk_idx, rv;
	int fw_size = kbflash_state.fw_size;
	uint32_t base_addr = kbflash_state.base_addr;
	uint16_t bl_addr = kbflash_state.bl_addr;
	struct stm32_bl_dev *dev = &kbflash_state.dev;

	uart_shell_rx_bypass(true);

	total_chunks = (fw_size + KBFLASH_CHUNK_SIZE - 1) / KBFLASH_CHUNK_SIZE;

	for (chunk_idx = 0; chunk_idx < total_chunks; chunk_idx++) {
		static uint8_t buf[KBFLASH_CHUNK_SIZE];
		int chunk_len = fw_size - chunk_idx * KBFLASH_CHUNK_SIZE;
		uint32_t addr;

		if (chunk_len > KBFLASH_CHUNK_SIZE)
			chunk_len = KBFLASH_CHUNK_SIZE;

		int got = kbflash_uart_read(buf, chunk_len);

		if (got != chunk_len) {
			uart_shell_rx_bypass(false);
			ccprintf("\nERROR: UART timeout at chunk %d (got %d/%d)\n",
				 chunk_idx, got, chunk_len);
			ccprintf("DONE\n");
			return;
		}

		/* Pad last chunk to 4-byte alignment with 0xFF */
		if (chunk_len < KBFLASH_CHUNK_SIZE) {
			int padded = (chunk_len + 3) & ~3;

			memset(buf + chunk_len, 0xFF, padded - chunk_len);
			chunk_len = padded;
		}

		addr = base_addr + (chunk_idx * KBFLASH_CHUNK_SIZE);
		rv = stm32_bl_write_memory(HID_KBD_I2C_PORT, bl_addr, dev,
					   addr, buf, chunk_len);

		if (rv) {
			uart_shell_rx_bypass(false);
			uart_write_char(KBFLASH_UART_NAK);
			ccprintf("\nERROR: I2C write failed at 0x%08x (%d)\n", addr, rv);
			ccprintf("DONE\n");
			return;
		}

		/* ACK to host — write directly to UART hardware.
		 * Stay in bypass mode so no console output interferes.
		 */
		uart_write_char(KBFLASH_UART_ACK);
		watchdog_reload();
	}

	uart_shell_rx_bypass(false);
	ccprintf("\nWrite complete: %d bytes\n", fw_size);

	/* Optional verify */
	if (kbflash_state.do_verify) {
		ccprintf("Verifying...\n");
		for (chunk_idx = 0; chunk_idx < total_chunks; chunk_idx++) {
			static uint8_t rbuf[KBFLASH_CHUNK_SIZE];
			int chunk_len = fw_size - chunk_idx * KBFLASH_CHUNK_SIZE;
			uint32_t addr;

			if (chunk_len > KBFLASH_CHUNK_SIZE)
				chunk_len = KBFLASH_CHUNK_SIZE;

			addr = base_addr + (chunk_idx * KBFLASH_CHUNK_SIZE);
			rv = stm32_bl_read_memory(HID_KBD_I2C_PORT, bl_addr,
						  dev, addr, rbuf, chunk_len);
			if (rv) {
				ccprintf("ERROR: verify read failed at 0x%08x (%d)\n", addr, rv);
				ccprintf("DONE\n");
				return;
			}
			watchdog_reload();
		}
		ccprintf("Verify read OK\n");
	}

	ccprintf("DONE\n");
}
DECLARE_DEFERRED(kbflash_deferred);

/* ------------------------------------------------------------------------- */

/* flash command, used by uart based flashing tool */
static int cmd_kbflash(int argc, const char **argv)
{
	char *e;

	if (argc < 2) {
		ccprintf("Usage: kbflash <size> [base_addr] [i2c_addr] [verify]\n");
		return EC_ERROR_PARAM_COUNT;
	}

	kbflash_state.fw_size = strtoi(argv[1], &e, 0);
	if (*e || kbflash_state.fw_size <= 0) {
		ccprintf("Bad size\n");
		return EC_ERROR_PARAM1;
	}

	kbflash_state.base_addr = STM32_BL_FLASH_BASE;
	kbflash_state.bl_addr = STM32_BL_I2C_ADDR;
	kbflash_state.do_verify = 0;

	if (argc >= 3) {
		kbflash_state.base_addr = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
	}

	if (argc >= 4) {
		int v = strtoi(argv[3], &e, 0);

		if (!*e)
			kbflash_state.bl_addr = v;
		else if (!strncmp(argv[3], "verify", 6))
			kbflash_state.do_verify = 1;
	}

	if (argc >= 5 && !strncmp(argv[4], "verify", 6))
		kbflash_state.do_verify = 1;

	/* Cancel the HID keyboard init retry — it fights for I2C5 */
	hook_call_deferred(&i2c_hid_kbd_init_deferred_data, -1);
	hid_initialized = false;
	crec_msleep(100);

	/* Init + erase run synchronously (shell thread is fine for I2C) */
	ccprintf("Connecting to STM32 bootloader at 0x%02x...\n", kbflash_state.bl_addr);

	int rv = stm32_bl_init(HID_KBD_I2C_PORT, kbflash_state.bl_addr, &kbflash_state.dev);

	if (rv) {
		ccprintf("ERROR: bootloader init failed (%d)\n", rv);
		return rv;
	}

	ccprintf("Device ID: 0x%04x, BL v%d.%d\n",
		 kbflash_state.dev.pid,
		 kbflash_state.dev.bl_version >> 4,
		 kbflash_state.dev.bl_version & 0xF);

	ccprintf("Erasing...\n");
	rv = stm32_bl_mass_erase(HID_KBD_I2C_PORT, kbflash_state.bl_addr,
				 &kbflash_state.dev);
	if (rv) {
		ccprintf("ERROR: erase failed (%d)\n", rv);
		return rv;
	}
	ccprintf("Erase done\n");

	/* Signal host, then hand off to deferred worker */
	ccprintf("READY\n");
	cflush();
	hook_call_deferred(&kbflash_deferred_data, 0);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kbflash, cmd_kbflash,
			"<size> [base_addr] [i2c_addr] [verify]",
			"Flash firmware to STM32 keyboard controller via I2C");

/* ------------------------------------------------------------------------- */

/*
 * Host-command interface to the same STM32 flash flow used by `kbflash`,
 * exposed to userland framework_tool over EC_CMD_KBD_FLASH_STM32. The
 * host drives the sub-verb sequence (Enter → Erase → Write… → optional
 * Read → Exit); this handler does the per-step work and reuses the
 * existing kbflash_state struct to hold the bootloader handle.
 */

static bool kbd_flash_stm32_active;

static enum ec_status kbd_flash_stm32_enter(struct host_cmd_handler_args *args)
{
	struct ec_response_kbd_flash_stm32_enter *r = args->response;
	int rv;

	if (args->response_max < sizeof(*r))
		return EC_RES_INVALID_PARAM;

	kbd_stm32_bootloader_enter_sequence();
	crec_msleep(50);

	kbflash_state.bl_addr = STM32_BL_I2C_ADDR;
	kbflash_state.base_addr = STM32_BL_FLASH_BASE;

	rv = stm32_bl_init(HID_KBD_I2C_PORT, kbflash_state.bl_addr,
			   &kbflash_state.dev);
	if (rv) {
		CPRINTS("KB hostcmd: bootloader init failed (%d)", rv);
		kbd_flash_stm32_active = false;
		return EC_RES_ERROR;
	}

	kbd_flash_stm32_active = true;
	r->pid = kbflash_state.dev.pid;
	r->bl_version = kbflash_state.dev.bl_version;
	r->max_chunk = KBD_FLASH_STM32_MAX_CHUNK;
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}

/* ------------------------------------------------------------------------- */

static enum ec_status kbd_flash_stm32_erase(struct host_cmd_handler_args *args)
{
	int rv;

	if (!kbd_flash_stm32_active)
		return EC_RES_ACCESS_DENIED;

	rv = stm32_bl_mass_erase(HID_KBD_I2C_PORT, kbflash_state.bl_addr,
				 &kbflash_state.dev);
	if (rv) {
		CPRINTS("KB hostcmd: erase failed (%d)", rv);
		return EC_RES_ERROR;
	}
	args->response_size = 0;
	return EC_RES_SUCCESS;
}

/* ------------------------------------------------------------------------- */

static enum ec_status kbd_flash_stm32_write(struct host_cmd_handler_args *args)
{
	const struct ec_params_kbd_flash_stm32_write *p = args->params;
	const uint8_t *data;
	int rv;

	if (!kbd_flash_stm32_active)
		return EC_RES_ACCESS_DENIED;
	if (args->params_size < sizeof(*p))
		return EC_RES_INVALID_PARAM;
	if (p->len == 0 || p->len > KBD_FLASH_STM32_MAX_CHUNK)
		return EC_RES_INVALID_PARAM;
	if (args->params_size != sizeof(*p) + p->len)
		return EC_RES_INVALID_PARAM;

	data = (const uint8_t *)(p + 1);
	rv = stm32_bl_write_memory(HID_KBD_I2C_PORT, kbflash_state.bl_addr,
				   &kbflash_state.dev, p->addr, data, p->len);
	if (rv) {
		CPRINTS("KB hostcmd: write 0x%08x (%d B) failed (%d)",
			p->addr, p->len, rv);
		return EC_RES_ERROR;
	}
	args->response_size = 0;
	return EC_RES_SUCCESS;
}

/* ------------------------------------------------------------------------- */

static enum ec_status kbd_flash_stm32_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_kbd_flash_stm32_read *p = args->params;
	int rv;

	if (!kbd_flash_stm32_active)
		return EC_RES_ACCESS_DENIED;
	if (args->params_size != sizeof(*p))
		return EC_RES_INVALID_PARAM;
	if (p->len == 0 || p->len > KBD_FLASH_STM32_MAX_CHUNK)
		return EC_RES_INVALID_PARAM;
	if (args->response_max < p->len)
		return EC_RES_INVALID_PARAM;

	rv = stm32_bl_read_memory(HID_KBD_I2C_PORT, kbflash_state.bl_addr,
				  &kbflash_state.dev, p->addr,
				  (uint8_t *)args->response, p->len);
	if (rv) {
		CPRINTS("KB hostcmd: read 0x%08x (%d B) failed (%d)",
			p->addr, p->len, rv);
		return EC_RES_ERROR;
	}
	args->response_size = p->len;
	return EC_RES_SUCCESS;
}

/* ------------------------------------------------------------------------- */

static enum ec_status kbd_flash_stm32_exit(struct host_cmd_handler_args *args)
{
	kbd_stm32_bootloader_exit_sequence();
	kbd_flash_stm32_active = false;
	args->response_size = 0;
	return EC_RES_SUCCESS;
}

/* ------------------------------------------------------------------------- */

static enum ec_status cmd_host_kbd_flash_stm32(struct host_cmd_handler_args *args)
{
	const uint8_t *p;

	if (args->params_size < 1)
		return EC_RES_INVALID_PARAM;

	p = args->params;
	switch (p[0]) {
	case KBD_FLASH_STM32_ENTER:
		return kbd_flash_stm32_enter(args);
	case KBD_FLASH_STM32_ERASE:
		return kbd_flash_stm32_erase(args);
	case KBD_FLASH_STM32_WRITE:
		return kbd_flash_stm32_write(args);
	case KBD_FLASH_STM32_READ:
		return kbd_flash_stm32_read(args);
	case KBD_FLASH_STM32_EXIT:
		return kbd_flash_stm32_exit(args);
	default:
		return EC_RES_INVALID_PARAM;
	}
}
DECLARE_HOST_COMMAND(EC_CMD_KBD_FLASH_STM32, cmd_host_kbd_flash_stm32, EC_VER_MASK(0));

#endif /* CONFIG_PLATFORM_EC_STM32_KEYBOARD */

/* ------------------------------------------------------------------------- */

/* Ops table for input_deck.c abstraction */

const struct input_deck input_deck_dahlia = {
	.power_on    = input_deck_dahlia_power_on,
	.power_off   = input_deck_dahlia_power_off,
	.resume      = input_deck_dahlia_resume,
	.suspend     = input_deck_dahlia_suspend,
	.kb_irq      = input_deck_dahlia_kb_irq,
	.set_kb_leds = input_deck_dahlia_caps_set_led,
	.lid_change  = input_deck_dahlia_tablet_or_lid_change,
};
