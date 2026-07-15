/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Input-deck abstraction. Holds pointers to the active deck's ops.
 */

#include "adc.h"
#include "board_adc.h"
#include "common.h"
#include "console.h"
#include "gpio/gpio_int.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "input_deck.h"
#include "keyboard_scan.h"
#include "tablet_mode.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ##args)

#if DT_NODE_EXISTS(DT_NODELABEL(dahlia_deck))
extern const struct input_deck input_deck_dahlia;
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(sunflower_deck))
extern const struct input_deck input_deck_sunflower;
#endif

static const struct input_deck *active_ops;
static int input_deck_board_id = BOARD_VERSION_UNKNOWN;

/* ------------------------------------------------------------------------ */

/* is there a deck attached? */
bool input_deck_is_present(void)
{
	/* TODO: add specialized detection for different keyboard controllers */
	return get_hardware_id(ADC_TOUCHPAD_ID) != BOARD_VERSION_NOT_INSTALLED;
}

/* ------------------------------------------------------------------------ */

/* detects the connected input deck, and enables keyscan if appropriate */
static void input_deck_detect(void)
{
	input_deck_board_id = get_hardware_id(ADC_TOUCHPAD_ID);
	const char *name = "unknown";

	switch (input_deck_board_id) {
#if DT_NODE_EXISTS(DT_NODELABEL(sunflower_deck))
	case BOARD_VERSION_10:
			active_ops = &input_deck_sunflower;
			name = "sunflower";
			break;
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(dahlia_deck))
	case BOARD_VERSION_11:
			active_ops = &input_deck_dahlia;
			name = "dahlia";
			break;
#endif
	default:
			active_ops = NULL;
	}

	/* cros-ec always enables ec keyscan, disable it and let decks turn it back on */
	keyboard_scan_enable(0, KB_SCAN_DISABLE_DISCONNECT);

	CPRINTS("Input deck : board id %d -> %s", input_deck_board_id, name);
}
DECLARE_HOOK(HOOK_INIT, input_deck_detect, HOOK_PRIO_DEFAULT);

/* ------------------------------------------------------------------------ */

void input_deck_resume(void)
{
	if (active_ops && active_ops->resume)
		active_ops->resume();
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, input_deck_resume, HOOK_PRIO_DEFAULT);

/* ------------------------------------------------------------------------ */

/* there is no hot plug support, yet */
void input_deck_power_on(void)
{
	if (active_ops && active_ops->power_on)
		active_ops->power_on();
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, input_deck_power_on, HOOK_PRIO_DEFAULT);

/* ------------------------------------------------------------------------ */

void input_deck_suspend(void)
{
	if (active_ops && active_ops->suspend)
		active_ops->suspend();
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, input_deck_suspend, HOOK_PRIO_DEFAULT);

/* ------------------------------------------------------------------------ */

void input_deck_power_off(void)
{
	if (active_ops && active_ops->power_off)
		active_ops->power_off();
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, input_deck_power_off, HOOK_PRIO_DEFAULT);

/* ----------------------------------------------------------------------- */

void input_deck_lid_change(void)
{
	/* Let the OS know if we're in tablet mode. */
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_pad_mode), tablet_get_mode());

	if (active_ops && active_ops->lid_change)
		active_ops->lid_change();
}
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, input_deck_lid_change, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_LID_CHANGE,         input_deck_lid_change, HOOK_PRIO_DEFAULT);

/* ---------------------------------------------------------------------- */

#if DT_NODE_EXISTS(DT_NODELABEL(dahlia_deck)) || \
	DT_NODE_EXISTS(DT_NODELABEL(sunflower_deck))

void input_deck_keyboard_interrupt(enum gpio_signal signal)
{
	if (active_ops && active_ops->kb_irq)
		active_ops->kb_irq();
}

/* ---------------------------------------------------------------------- */

/* The 8042 emulator calls a function by this name when the OS updates LED state. */
/* The function name is decided by 8042 subsystem */
void board_caps_led_control(int data)
{
	if (active_ops && active_ops->set_kb_leds)
		active_ops->set_kb_leds(data);
}
#endif
