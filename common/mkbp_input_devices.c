/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Input devices using Matrix Keyboard Protocol [MKBP] events for Chrome EC */

#include "base_state.h"
#include "button.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_mkbp.h"
#include "lid_switch.h"
#include "mkbp_event.h"
#include "mkbp_fifo.h"
#include "mkbp_input_devices.h"
#include "power_button.h"
#include "tablet_mode.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ## args)

/* Buttons and switch state. */
static uint32_t mkbp_button_state;
static uint32_t mkbp_switch_state;

static bool mkbp_init_done;

uint32_t mkbp_get_switch_state(void)
{
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

void mkbp_update_switches(uint32_t sw, int state)
{
	mkbp_switch_state &= ~BIT(sw);
	mkbp_switch_state |= (!!state << sw);

	CPRINTS("mkbp switches: %x", mkbp_switch_state);

	/*
	 * Only inform AP mkbp changes when all switches initialized, in case
	 * of the middle states causing the weird behaviour in the AP side,
	 * especially when sysjumped while AP up.
	 */
	if (mkbp_init_done)
		mkbp_fifo_add(EC_MKBP_EVENT_SWITCH,
				(const uint8_t *)&mkbp_switch_state);
}


/*****************************************************************************/
/* Hooks */

#ifdef CONFIG_POWER_BUTTON
/**
 * Handle power button changing state.
 */
static void keyboard_power_button(void)
{
	mkbp_button_update(KEYBOARD_BUTTON_POWER,
			       power_button_is_pressed());
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, keyboard_power_button,
	     HOOK_PRIO_DEFAULT);
#endif /* defined(CONFIG_POWER_BUTTON) */

#ifdef CONFIG_LID_SWITCH
/**
 * Handle lid changing state.
 */
static void mkbp_lid_change(void)
{
	mkbp_update_switches(EC_MKBP_LID_OPEN, lid_is_open());
}
DECLARE_HOOK(HOOK_LID_CHANGE, mkbp_lid_change, HOOK_PRIO_LAST);
DECLARE_HOOK(HOOK_INIT, mkbp_lid_change, HOOK_PRIO_INIT_LID+1);
#endif

#ifdef CONFIG_TABLET_MODE_SWITCH
static void mkbp_tablet_mode_change(void)
{
	mkbp_update_switches(EC_MKBP_TABLET_MODE, tablet_get_mode());
}
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, mkbp_tablet_mode_change, HOOK_PRIO_LAST);
DECLARE_HOOK(HOOK_INIT, mkbp_tablet_mode_change, HOOK_PRIO_INIT_LID+1);
#endif

#ifdef CONFIG_BASE_ATTACHED_SWITCH
static void mkbp_base_attached_change(void)
{
	mkbp_update_switches(EC_MKBP_BASE_ATTACHED, base_get_state());
}
DECLARE_HOOK(HOOK_BASE_ATTACHED_CHANGE, mkbp_base_attached_change,
	     HOOK_PRIO_LAST);
DECLARE_HOOK(HOOK_INIT, mkbp_base_attached_change, HOOK_PRIO_INIT_LID+1);
#endif

static void mkbp_report_switch_on_init(void)
{
	/* All switches initialized, report switch state to AP */
	mkbp_init_done = true;
	mkbp_fifo_add(EC_MKBP_EVENT_SWITCH,
			(const uint8_t *)&mkbp_switch_state);
}
DECLARE_HOOK(HOOK_INIT, mkbp_report_switch_on_init, HOOK_PRIO_LAST);

#ifdef CONFIG_EMULATED_SYSRQ
void host_send_sysrq(uint8_t key)
{
	uint32_t value = key;

	mkbp_fifo_add(EC_MKBP_EVENT_SYSRQ, (const uint8_t *)&value);
}
#endif

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
