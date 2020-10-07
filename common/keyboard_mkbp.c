/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MKBP keyboard protocol
 */

#include "atomic.h"
#include "base_state.h"
#include "button.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_config.h"
#include "keyboard_mkbp.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "keyboard_test.h"
#include "lid_switch.h"
#include "mkbp_event.h"
#include "power_button.h"
#include "system.h"
#include "tablet_mode.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYBOARD, outstr)
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ## args)

/*
 * Common FIFO depth.  This needs to be big enough not to overflow if a
 * series of keys is pressed in rapid succession and the kernel is too busy
 * to read them out right away.
 *
 * RAM usage is (depth * #cols); A 16-entry FIFO will consume 16x13=208 bytes,
 * which is non-trivial but not horrible.
 */

#define FIFO_DEPTH 16

/* Changes to col,row here need to also be reflected in kernel.
 * drivers/input/mkbp.c ... see KEY_BATTERY.
 */
#define BATTERY_KEY_COL 0
#define BATTERY_KEY_ROW 7
#define BATTERY_KEY_ROW_MASK BIT(BATTERY_KEY_ROW)

static uint32_t fifo_start;	/* first entry */
static uint32_t fifo_end;	/* last entry */
static uint32_t fifo_entries;	/* number of existing entries */
static struct ec_response_get_next_event fifo[FIFO_DEPTH];
/*
 * Mutex for critical sections of mkbp_fifo_add(), which is called
 * from various tasks.
 */
static struct mutex fifo_add_mutex;
/*
 * Mutex for critical sections of fifo_remove(), which is called from the
 * hostcmd task and from keyboard_clear_buffer().
 */
static struct mutex fifo_remove_mutex;

/* Button and switch state. */
static uint32_t mkbp_button_state;
static uint32_t mkbp_switch_state;
#ifndef HAS_TASK_KEYSCAN
/* Keys simulated-pressed */
static uint8_t __bss_slow simulated_key[KEYBOARD_COLS_MAX];
uint8_t keyboard_cols = KEYBOARD_COLS_MAX;
#endif /* !defined(HAS_TASK_KEYSCAN) */

/* Config for mkbp protocol; does not include fields from scan config */
struct ec_mkbp_protocol_config {
	uint32_t valid_mask;	/* valid fields */
	uint8_t flags;		/* some flags (enum mkbp_config_flags) */
	uint8_t valid_flags;	/* which flags are valid */

	/* maximum depth to allow for fifo (0 = no keyscan output) */
	uint8_t fifo_max_depth;
} __packed;

static struct ec_mkbp_protocol_config config = {
	.valid_mask = EC_MKBP_VALID_SCAN_PERIOD | EC_MKBP_VALID_POLL_TIMEOUT |
		EC_MKBP_VALID_MIN_POST_SCAN_DELAY |
		EC_MKBP_VALID_OUTPUT_SETTLE | EC_MKBP_VALID_DEBOUNCE_DOWN |
		EC_MKBP_VALID_DEBOUNCE_UP | EC_MKBP_VALID_FIFO_MAX_DEPTH,
	.valid_flags = EC_MKBP_FLAGS_ENABLE,
	.flags = EC_MKBP_FLAGS_ENABLE,
	.fifo_max_depth = FIFO_DEPTH,
};

static int get_data_size(enum ec_mkbp_event e)
{
	switch (e) {
	case EC_MKBP_EVENT_KEY_MATRIX:
		return keyboard_cols;

#ifdef CONFIG_HOST_EVENT64
	case EC_MKBP_EVENT_HOST_EVENT64:
		return sizeof(uint64_t);
#endif

	case EC_MKBP_EVENT_HOST_EVENT:
	case EC_MKBP_EVENT_BUTTON:
	case EC_MKBP_EVENT_SWITCH:
	case EC_MKBP_EVENT_SYSRQ:
		return sizeof(uint32_t);
	default:
		/* For unknown types, say it's 0. */
		return 0;
	}
}

/**
 * Pop MKBP event data from FIFO
 *
 * @return EC_SUCCESS if entry popped, EC_ERROR_UNKNOWN if FIFO is empty
 */
static int fifo_remove(uint8_t *buffp)
{
	int size;

	mutex_lock(&fifo_remove_mutex);
	if (!fifo_entries) {
		/* no entry remaining in FIFO : return last known state */
		int last = (fifo_start + FIFO_DEPTH - 1) % FIFO_DEPTH;

		size = get_data_size(fifo[last].event_type);

		memcpy(buffp, &fifo[last].data, size);
		mutex_unlock(&fifo_remove_mutex);

		/*
		 * Bail out without changing any FIFO indices and let the
		 * caller know something strange happened. The buffer will
		 * will contain the last known state of the keyboard.
		 */
		return EC_ERROR_UNKNOWN;
	}

	/* Return just the event data. */
	if (buffp) {
		size = get_data_size(fifo[fifo_start].event_type);
		/* skip over event_type. */
		memcpy(buffp, &fifo[fifo_start].data, size);
	}

	fifo_start = (fifo_start + 1) % FIFO_DEPTH;
	deprecated_atomic_sub(&fifo_entries, 1);
	mutex_unlock(&fifo_remove_mutex);

	return EC_SUCCESS;
}

/*****************************************************************************/
/* Interface */

void keyboard_clear_buffer(void)
{
	int i, new_fifo_entries = 0;

	CPRINTS("clear keyboard MKBP fifo");

	/*
	 * Order of these locks is important to prevent deadlock since
	 * mkbp_fifo_add() may call fifo_remove().
	 */
	mutex_lock(&fifo_add_mutex);
	mutex_lock(&fifo_remove_mutex);

	/* Reset the end position */
	fifo_end = fifo_start;

	for (i = 0; i < fifo_entries; i++) {
		int cur = (fifo_start + i) % FIFO_DEPTH;

		/* Drop keyboard events */
		if (fifo[cur].event_type == EC_MKBP_EVENT_KEY_MATRIX)
			continue;

		/* And move other events to the front */
		memmove(&fifo[fifo_end], &fifo[cur], sizeof(fifo[cur]));
		fifo_end = (fifo_end + 1) % FIFO_DEPTH;
		++new_fifo_entries;
	}
	fifo_entries = new_fifo_entries;

	mutex_unlock(&fifo_remove_mutex);
	mutex_unlock(&fifo_add_mutex);
}

void mkbp_clear_fifo(void)
{
	int i;

	CPRINTS("clear MKBP fifo");

	/*
	 * Order of these locks is important to prevent deadlock since
	 * mkbp_fifo_add() may call fifo_remove().
	 */
	mutex_lock(&fifo_add_mutex);
	mutex_lock(&fifo_remove_mutex);

	fifo_start = 0;
	fifo_end = 0;
	/* This assignment is safe since both mutexes are held. */
	fifo_entries = 0;
	for (i = 0; i < FIFO_DEPTH; i++)
		memset(&fifo[i], 0, sizeof(struct ec_response_get_next_event));

	mutex_unlock(&fifo_remove_mutex);
	mutex_unlock(&fifo_add_mutex);
}

test_mockable int keyboard_fifo_add(const uint8_t *buffp)
{
	return mkbp_fifo_add((uint8_t)EC_MKBP_EVENT_KEY_MATRIX, buffp);
}

test_mockable int mkbp_fifo_add(uint8_t event_type, const uint8_t *buffp)
{
	uint8_t size;

	/*
	 * If the data is a keyboard matrix and the keyboard protocol is not
	 * enabled, don't save the state to the FIFO or trigger an interrupt.
	 */
	if (!(config.flags & EC_MKBP_FLAGS_ENABLE) &&
	    (event_type == EC_MKBP_EVENT_KEY_MATRIX))
		return EC_SUCCESS;

	mutex_lock(&fifo_add_mutex);
	if (fifo_entries >= config.fifo_max_depth) {
		mutex_unlock(&fifo_add_mutex);
		CPRINTS("MKBP common FIFO depth %d reached",
			config.fifo_max_depth);

		return EC_ERROR_OVERFLOW;
	}

	size = get_data_size(event_type);
	fifo[fifo_end].event_type = event_type;
	memcpy(&fifo[fifo_end].data, buffp, size);
	fifo_end = (fifo_end + 1) % FIFO_DEPTH;
	deprecated_atomic_add(&fifo_entries, 1);

	/*
	 * If our event didn't generate an interrupt then the host is still
	 * asleep. In this case, we don't want to queue our event, except if
	 * another event just woke the host (and wake is already in progress).
	 */
	if (!mkbp_send_event(event_type) && fifo_entries == 1)
		fifo_remove(NULL);

	mutex_unlock(&fifo_add_mutex);
	return EC_SUCCESS;
}

void mkbp_update_switches(uint32_t sw, int state)
{

	mkbp_switch_state &= ~BIT(sw);
	mkbp_switch_state |= (!!state << sw);

	mkbp_fifo_add(EC_MKBP_EVENT_SWITCH,
		      (const uint8_t *)&mkbp_switch_state);
}

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

void keyboard_update_button(enum keyboard_button_type button, int is_pressed)
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

	CPRINTS("buttons: %x", mkbp_button_state);

	/* Add the new state to the FIFO. */
	mkbp_fifo_add(EC_MKBP_EVENT_BUTTON,
		      (const uint8_t *)&mkbp_button_state);
}

#ifdef CONFIG_EMULATED_SYSRQ
void host_send_sysrq(uint8_t key)
{
	uint32_t value = key;

	mkbp_fifo_add(EC_MKBP_EVENT_SYSRQ, (const uint8_t *)&value);
}
#endif

#ifdef CONFIG_POWER_BUTTON
/**
 * Handle power button changing state.
 */
static void keyboard_power_button(void)
{
	keyboard_update_button(KEYBOARD_BUTTON_POWER,
			       power_button_is_pressed());
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, keyboard_power_button,
	     HOOK_PRIO_DEFAULT);
#endif /* defined(CONFIG_POWER_BUTTON) */

static int get_next_event(uint8_t *out, enum ec_mkbp_event evt)
{
	uint8_t t = fifo[fifo_start].event_type;
	uint8_t size;

	if (!fifo_entries)
		return -1;

	/*
	 * We need to peek at the next event to check that we were called with
	 * the correct event.
	 */
	if (t != (uint8_t)evt) {
		/*
		 * We were called with the wrong event.  The next element in the
		 * FIFO's event type doesn't match with what we were called
		 * with.  Return an error that we're busy.  The caller will need
		 * to call us with the correct event first.
		 */
		return -EC_ERROR_BUSY;
	}

	fifo_remove(out);

	/* Keep sending events if FIFO is not empty */
	if (fifo_entries)
		mkbp_send_event(fifo[fifo_start].event_type);

	/* Return the correct size of the data. */
	size = get_data_size(t);
	if (size)
		return size;
	else
		return -EC_ERROR_UNKNOWN;
}

static int keyboard_get_next_event(uint8_t *out)
{
	return get_next_event(out, EC_MKBP_EVENT_KEY_MATRIX);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_KEY_MATRIX, keyboard_get_next_event);

static int button_get_next_event(uint8_t *out)
{
	return get_next_event(out, EC_MKBP_EVENT_BUTTON);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_BUTTON, button_get_next_event);

static int switch_get_next_event(uint8_t *out)
{
	return get_next_event(out, EC_MKBP_EVENT_SWITCH);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_SWITCH, switch_get_next_event);

#ifdef CONFIG_EMULATED_SYSRQ
static int sysrq_get_next_event(uint8_t *out)
{
	return get_next_event(out, EC_MKBP_EVENT_SYSRQ);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_SYSRQ, sysrq_get_next_event);
#endif

void keyboard_send_battery_key(void)
{
	uint8_t state[KEYBOARD_COLS_MAX];

	/* Copy debounced state and add battery pseudo-key */
	memcpy(state, keyboard_scan_get_state(), sizeof(state));
	state[BATTERY_KEY_COL] ^= BATTERY_KEY_ROW_MASK;

	/* Add to FIFO only if AP is on or else it will wake from suspend */
	if (chipset_in_state(CHIPSET_STATE_ON))
		keyboard_fifo_add(state);
}

void clear_typematic_key(void)
{ }

/*****************************************************************************/
/* Host commands */
static uint32_t get_supported_buttons(void)
{
	uint32_t val = 0;

#ifdef CONFIG_VOLUME_BUTTONS
	val |= BIT(EC_MKBP_VOL_UP) | BIT(EC_MKBP_VOL_DOWN);
#endif /* defined(CONFIG_VOLUME_BUTTONS) */

#ifdef CONFIG_DEDICATED_RECOVERY_BUTTON
	val |= BIT(EC_MKBP_RECOVERY);
#endif /* defined(CONFIG_DEDICATED_RECOVERY_BUTTON) */

#ifdef CONFIG_POWER_BUTTON
	val |= BIT(EC_MKBP_POWER_BUTTON);
#endif /* defined(CONFIG_POWER_BUTTON) */

	return val;
}

static uint32_t get_supported_switches(void)
{
	uint32_t val = 0;

#ifdef CONFIG_LID_SWITCH
	val |= BIT(EC_MKBP_LID_OPEN);
#endif
#ifdef CONFIG_TABLET_MODE_SWITCH
	val |= BIT(EC_MKBP_TABLET_MODE);
#endif
#ifdef CONFIG_BASE_ATTACHED_SWITCH
	val |= BIT(EC_MKBP_BASE_ATTACHED);
#endif
	return val;
}

static enum ec_status mkbp_get_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_mkbp_info *p = args->params;

	if (args->params_size == 0 || p->info_type == EC_MKBP_INFO_KBD) {
		struct ec_response_mkbp_info *r = args->response;

		/* Version 0 just returns info about the keyboard. */
		r->rows = KEYBOARD_ROWS;
		r->cols = keyboard_cols;
		/* This used to be "switches" which was previously 0. */
		r->reserved = 0;

		args->response_size = sizeof(struct ec_response_mkbp_info);
	} else {
		union ec_response_get_next_data *r = args->response;

		/* Version 1 (other than EC_MKBP_INFO_KBD) */
		switch (p->info_type) {
		case EC_MKBP_INFO_SUPPORTED:
			switch (p->event_type) {
			case EC_MKBP_EVENT_BUTTON:
				r->buttons = get_supported_buttons();
				args->response_size = sizeof(r->buttons);
				break;

			case EC_MKBP_EVENT_SWITCH:
				r->switches = get_supported_switches();
				args->response_size = sizeof(r->switches);
				break;

			default:
				/* Don't care for now for other types. */
				return EC_RES_INVALID_PARAM;
			}
			break;

		case EC_MKBP_INFO_CURRENT:
			switch (p->event_type) {
#ifdef HAS_TASK_KEYSCAN
			case EC_MKBP_EVENT_KEY_MATRIX:
				memcpy(r->key_matrix, keyboard_scan_get_state(),
				       sizeof(r->key_matrix));
				args->response_size = sizeof(r->key_matrix);
				break;
#endif
			case EC_MKBP_EVENT_HOST_EVENT:
				r->host_event = (uint32_t)host_get_events();
				args->response_size = sizeof(r->host_event);
				break;

#ifdef CONFIG_HOST_EVENT64
			case EC_MKBP_EVENT_HOST_EVENT64:
				r->host_event64 = host_get_events();
				args->response_size = sizeof(r->host_event64);
				break;
#endif

			case EC_MKBP_EVENT_BUTTON:
				r->buttons = mkbp_button_state;
				args->response_size = sizeof(r->buttons);
				break;

			case EC_MKBP_EVENT_SWITCH:
				r->switches = mkbp_switch_state;
				args->response_size = sizeof(r->switches);
				break;

			default:
				/* Doesn't make sense for other event types. */
				return EC_RES_INVALID_PARAM;
			}
			break;

		default:
			/* Unsupported query. */
			return EC_RES_ERROR;
		}
	}
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_INFO, mkbp_get_info,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

#ifndef HAS_TASK_KEYSCAN
/* For boards without a keyscan task, try and simulate keyboard presses. */
static void simulate_key(int row, int col, int pressed)
{
	if ((simulated_key[col] & BIT(row)) == ((pressed ? 1 : 0) << row))
		return;  /* No change */

	simulated_key[col] &= ~BIT(row);
	if (pressed)
		simulated_key[col] |= BIT(row);

	keyboard_fifo_add(simulated_key);
}

static int command_mkbp_keyboard_press(int argc, char **argv)
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
			"[col row [0 | 1]]",
			"Simulate keypress");
#endif /* !defined(HAS_TASK_KEYSCAN) */

#ifdef HAS_TASK_KEYSCAN
static void set_keyscan_config(const struct ec_mkbp_config *src,
			       struct ec_mkbp_protocol_config *dst,
			       uint32_t valid_mask, uint8_t new_flags)
{
	struct keyboard_scan_config *ksc = keyboard_scan_get_config();

	if (valid_mask & EC_MKBP_VALID_SCAN_PERIOD)
		ksc->scan_period_us = src->scan_period_us;

	if (valid_mask & EC_MKBP_VALID_POLL_TIMEOUT)
		ksc->poll_timeout_us = src->poll_timeout_us;

	if (valid_mask & EC_MKBP_VALID_MIN_POST_SCAN_DELAY) {
		/*
		 * Key scanning is high priority, so we should require at
		 * least 100us min delay here. Setting this to 0 will cause
		 * watchdog events. Use 200 to be safe.
		 */
		ksc->min_post_scan_delay_us =
			MAX(src->min_post_scan_delay_us, 200);
	}

	if (valid_mask & EC_MKBP_VALID_OUTPUT_SETTLE)
		ksc->output_settle_us = src->output_settle_us;

	if (valid_mask & EC_MKBP_VALID_DEBOUNCE_DOWN)
		ksc->debounce_down_us = src->debounce_down_us;

	if (valid_mask & EC_MKBP_VALID_DEBOUNCE_UP)
		ksc->debounce_up_us = src->debounce_up_us;

	/*
	 * If we just enabled key scanning, kick the task so that it will
	 * fall out of the task_wait_event() in keyboard_scan_task().
	 */
	if ((new_flags & EC_MKBP_FLAGS_ENABLE) &&
			!(dst->flags & EC_MKBP_FLAGS_ENABLE))
		task_wake(TASK_ID_KEYSCAN);
}

static void get_keyscan_config(struct ec_mkbp_config *dst)
{
	const struct keyboard_scan_config *ksc = keyboard_scan_get_config();

	/* Copy fields from keyscan config to mkbp config */
	dst->output_settle_us = ksc->output_settle_us;
	dst->debounce_down_us = ksc->debounce_down_us;
	dst->debounce_up_us = ksc->debounce_up_us;
	dst->scan_period_us = ksc->scan_period_us;
	dst->min_post_scan_delay_us = ksc->min_post_scan_delay_us;
	dst->poll_timeout_us = ksc->poll_timeout_us;
}

/**
 * Copy keyscan configuration from one place to another according to flags
 *
 * This is like a structure copy, except that only selected fields are
 * copied.
 *
 * @param src		Source config
 * @param dst		Destination config
 * @param valid_mask	Bits representing which fields to copy - each bit is
 *			from enum mkbp_config_valid
 * @param valid_flags	Bit mask controlling flags to copy. Any 1 bit means
 *			that the corresponding bit in src->flags is copied
 *			over to dst->flags
 */
static void keyscan_copy_config(const struct ec_mkbp_config *src,
				 struct ec_mkbp_protocol_config *dst,
				 uint32_t valid_mask, uint8_t valid_flags)
{
	uint8_t new_flags;

	if (valid_mask & EC_MKBP_VALID_FIFO_MAX_DEPTH) {
		/* Validity check for fifo depth */
		dst->fifo_max_depth = MIN(src->fifo_max_depth,
					  FIFO_DEPTH);
	}

	new_flags = dst->flags & ~valid_flags;
	new_flags |= src->flags & valid_flags;

	set_keyscan_config(src, dst, valid_mask, new_flags);
	dst->flags = new_flags;
}

static enum ec_status
host_command_mkbp_set_config(struct host_cmd_handler_args *args)
{
	const struct ec_params_mkbp_set_config *req = args->params;

	keyscan_copy_config(&req->config, &config,
			    config.valid_mask & req->config.valid_mask,
			    config.valid_flags & req->config.valid_flags);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_SET_CONFIG,
		     host_command_mkbp_set_config,
		     EC_VER_MASK(0));

static enum ec_status
host_command_mkbp_get_config(struct host_cmd_handler_args *args)
{
	struct ec_response_mkbp_get_config *resp = args->response;
	struct ec_mkbp_config *dst = &resp->config;

	memcpy(&resp->config, &config, sizeof(config));

	/* Copy fields from mkbp protocol config to mkbp config */
	dst->valid_mask = config.valid_mask;
	dst->flags = config.flags;
	dst->valid_flags = config.valid_flags;
	dst->fifo_max_depth = config.fifo_max_depth;

	get_keyscan_config(dst);

	args->response_size = sizeof(*resp);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_GET_CONFIG,
		     host_command_mkbp_get_config,
		     EC_VER_MASK(0));
#endif /* HAS_TASK_KEYSCAN */
