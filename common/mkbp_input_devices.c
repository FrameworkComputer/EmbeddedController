/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Input devices using Matrix Keyboard Protocol [MKBP] events for Chrome EC */

#include "base_state.h"
#include "body_detection.h"
#include "button.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_mkbp.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "mkbp_event.h"
#include "mkbp_fifo.h"
#include "mkbp_input_devices.h"
#include "power_button.h"
#include "tablet_mode.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ##args)

/* Buttons state. */
static uint32_t mkbp_button_state;

static bool mkbp_init_done;

uint32_t mkbp_get_switch_state(void)
{
	uint32_t mkbp_switch_state = 0;

	/*
	 * These all read already debounced states and cause no side effects
	 * or latency.
	 */
	if (IS_ENABLED(CONFIG_LID_SWITCH))
		mkbp_switch_state |= lid_is_open() << EC_MKBP_LID_OPEN;
	if (IS_ENABLED(CONFIG_TABLET_MODE_SWITCH))
		mkbp_switch_state |= tablet_get_mode() << EC_MKBP_TABLET_MODE;
	if (IS_ENABLED(CONFIG_BASE_ATTACHED_SWITCH))
		mkbp_switch_state |= base_get_state() << EC_MKBP_BASE_ATTACHED;
	if (IS_ENABLED(CONFIG_BODY_DETECTION_NOTIFY_MKBP))
		mkbp_switch_state |= body_detect_get_state()
				     << EC_MKBP_FRONT_PROXIMITY;
	return mkbp_switch_state;
};

uint32_t mkbp_get_button_state(void)
{
	return mkbp_button_state;
};

void mkbp_button_update(enum keyboard_button_type button, int is_pressed)
{
	switch (button) {
	case KEYBOARD_BUTTON_POWER:
		mkbp_button_state &= ~BIT(EC_MKBP_POWER_BUTTON);
		mkbp_button_state |= (is_pressed << EC_MKBP_POWER_BUTTON);
		break;

	case KEYBOARD_BUTTON_VOLUME_UP:
		mkbp_button_state &= ~BIT(EC_MKBP_VOL_UP);
		mkbp_button_state |= (is_pressed << EC_MKBP_VOL_UP);
		break;

	case KEYBOARD_BUTTON_VOLUME_DOWN:
		mkbp_button_state &= ~BIT(EC_MKBP_VOL_DOWN);
		mkbp_button_state |= (is_pressed << EC_MKBP_VOL_DOWN);
		break;

	case KEYBOARD_BUTTON_RECOVERY:
		mkbp_button_state &= ~BIT(EC_MKBP_RECOVERY);
		mkbp_button_state |= (is_pressed << EC_MKBP_RECOVERY);
		break;

	default:
		/* ignored. */
		return;
	}

	CPRINTS("mkbp buttons: %x", mkbp_button_state);

	mkbp_fifo_add(EC_MKBP_EVENT_BUTTON,
		      (const uint8_t *)&mkbp_button_state);
};

void mkbp_update_switches(void)
{
	/*
	 * Only inform AP mkbp changes when all switches initialized, in case
	 * of the middle states causing the weird behaviour in the AP side,
	 * especially when sysjumped while AP up.
	 */
	if (mkbp_init_done) {
		uint32_t mkbp_switch_state = mkbp_get_switch_state();

		CPRINTS("mkbp switches: %x", mkbp_switch_state);
		mkbp_fifo_add(EC_MKBP_EVENT_SWITCH,
			      (const uint8_t *)&mkbp_switch_state);
	}
}

/*****************************************************************************/
/* Hooks */

#ifdef CONFIG_POWER_BUTTON
/**
 * Handle power button changing state.
 */
static void keyboard_power_button(void)
{
	mkbp_button_update(KEYBOARD_BUTTON_POWER, power_button_is_pressed());
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, keyboard_power_button,
	     HOOK_PRIO_DEFAULT);
#endif /* defined(CONFIG_POWER_BUTTON) */

#ifdef CONFIG_LID_SWITCH
/**
 * Handle lid changing state.
 */
DECLARE_HOOK(HOOK_LID_CHANGE, mkbp_update_switches, HOOK_PRIO_LAST);
#endif

#ifdef CONFIG_TABLET_MODE_SWITCH
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, mkbp_update_switches, HOOK_PRIO_LAST);
#endif

#ifdef CONFIG_BASE_ATTACHED_SWITCH
DECLARE_HOOK(HOOK_BASE_ATTACHED_CHANGE, mkbp_update_switches, HOOK_PRIO_LAST);
#endif

#ifdef CONFIG_BODY_DETECTION_NOTIFY_MKBP
DECLARE_HOOK(HOOK_BODY_DETECT_CHANGE, mkbp_update_switches, HOOK_PRIO_LAST);
#endif

static void mkbp_report_switch_on_init(void)
{
	/* All switches initialized, report switch state to AP */
	mkbp_init_done = true;
	mkbp_update_switches();
}
DECLARE_HOOK(HOOK_INIT, mkbp_report_switch_on_init, HOOK_PRIO_LAST);

/*****************************************************************************/
/* Events */

static int mkbp_button_get_next_event(uint8_t *out)
{
	return mkbp_fifo_get_next_event(out, EC_MKBP_EVENT_BUTTON);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_BUTTON, mkbp_button_get_next_event);

static int switch_get_next_event(uint8_t *out)
{
	return mkbp_fifo_get_next_event(out, EC_MKBP_EVENT_SWITCH);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_SWITCH, switch_get_next_event);

#ifdef CONFIG_EMULATED_SYSRQ
static int sysrq_get_next_event(uint8_t *out)
{
	return mkbp_fifo_get_next_event(out, EC_MKBP_EVENT_SYSRQ);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_SYSRQ, sysrq_get_next_event);
#endif

/************************ Keyboard press simulation ************************/
#ifndef HAS_TASK_KEYSCAN
/* Keys simulated-pressed */
static uint8_t simulated_key[KEYBOARD_COLS_MAX];
uint8_t keyboard_cols = KEYBOARD_COLS_MAX;

/* For boards without a keyscan task, try and simulate keyboard presses. */
static void simulate_key(int row, int col, int pressed)
{
	if ((simulated_key[col] & BIT(row)) == ((pressed ? 1 : 0) << row))
		return; /* No change */

	simulated_key[col] &= ~BIT(row);
	if (pressed)
		simulated_key[col] |= BIT(row);

	mkbp_fifo_add((uint8_t)EC_MKBP_EVENT_KEY_MATRIX, simulated_key);
}

static int command_mkbp_keyboard_press(int argc, const char **argv)
{
	if (argc == 1) {
		int i, j;

		ccputs("Simulated keys:\n");
		for (i = 0; i < keyboard_cols; ++i) {
			if (simulated_key[i] == 0)
				continue;
			for (j = 0; j < KEYBOARD_ROWS; ++j)
				if (simulated_key[i] & BIT(j))
					ccprintf("\t%d %d\n", i, j);
		}

	} else if (argc == 3 || argc == 4) {
		int r, c, p;
		char *e;

		c = strtoi(argv[1], &e, 0);
		if (*e || c < 0 || c >= keyboard_cols)
			return EC_ERROR_PARAM1;

		r = strtoi(argv[2], &e, 0);
		if (*e || r < 0 || r >= KEYBOARD_ROWS)
			return EC_ERROR_PARAM2;

		if (argc == 3) {
			/* Simulate a press and release */
			simulate_key(r, c, 1);
			simulate_key(r, c, 0);
		} else {
			p = strtoi(argv[3], &e, 0);
			if (*e || p < 0 || p > 1)
				return EC_ERROR_PARAM3;

			simulate_key(r, c, p);
		}
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(kbpress, command_mkbp_keyboard_press,
			"[col row [0 | 1]]", "Simulate keypress");

#endif /* !defined(HAS_TASK_KEYSCAN) */
