/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MKBP keyboard protocol
 */

#include "atomic.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "host_command.h"
#include "keyboard_config.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "keyboard_test.h"
#include "mkbp_event.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYBOARD, outstr)
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ## args)

/*
 * Keyboard FIFO depth.  This needs to be big enough not to overflow if a
 * series of keys is pressed in rapid succession and the kernel is too busy
 * to read them out right away.
 *
 * RAM usage is (depth * #cols); see kb_fifo[][] below.  A 16-entry FIFO will
 * consume 16x13=208 bytes, which is non-trivial but not horrible.
 */
#define KB_FIFO_DEPTH 16

/* Changes to col,row here need to also be reflected in kernel.
 * drivers/input/mkbp.c ... see KEY_BATTERY.
 */
#define BATTERY_KEY_COL 0
#define BATTERY_KEY_ROW 7
#define BATTERY_KEY_ROW_MASK (1 << BATTERY_KEY_ROW)

static uint32_t kb_fifo_start;		/* first entry */
static uint32_t kb_fifo_end;		/* last entry */
static uint32_t kb_fifo_entries;	/* number of existing entries */
static uint8_t kb_fifo[KB_FIFO_DEPTH][KEYBOARD_COLS];
static struct mutex fifo_mutex;

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
	.fifo_max_depth = KB_FIFO_DEPTH,
};

/**
 * Pop keyboard state from FIFO
 *
 * @return EC_SUCCESS if entry popped, EC_ERROR_UNKNOWN if FIFO is empty
 */
static int kb_fifo_remove(uint8_t *buffp)
{
	if (!kb_fifo_entries) {
		/* no entry remaining in FIFO : return last known state */
		int last = (kb_fifo_start + KB_FIFO_DEPTH - 1) % KB_FIFO_DEPTH;
		memcpy(buffp, kb_fifo[last], KEYBOARD_COLS);

		/*
		 * Bail out without changing any FIFO indices and let the
		 * caller know something strange happened. The buffer will
		 * will contain the last known state of the keyboard.
		 */
		return EC_ERROR_UNKNOWN;
	}
	memcpy(buffp, kb_fifo[kb_fifo_start], KEYBOARD_COLS);

	kb_fifo_start = (kb_fifo_start + 1) % KB_FIFO_DEPTH;

	atomic_sub(&kb_fifo_entries, 1);

	return EC_SUCCESS;
}

/**
 * Assert host keyboard interrupt line.
 */
static void set_host_interrupt(int active)
{
	/* interrupt host by using active low EC_INT signal */
	gpio_set_level(GPIO_EC_INT, !active);
}

/*****************************************************************************/
/* Interface */

void keyboard_clear_buffer(void)
{
	int i;

	CPRINTS("clearing keyboard fifo");

	kb_fifo_start = 0;
	kb_fifo_end = 0;
	kb_fifo_entries = 0;
	for (i = 0; i < KB_FIFO_DEPTH; i++)
		memset(kb_fifo[i], 0, KEYBOARD_COLS);
}

test_mockable int keyboard_fifo_add(const uint8_t *buffp)
{
	int ret = EC_SUCCESS;

	/*
	 * If keyboard protocol is not enabled, don't save the state to the
	 * FIFO or trigger an interrupt.
	 */
	if (!(config.flags & EC_MKBP_FLAGS_ENABLE))
		return EC_SUCCESS;

	if (kb_fifo_entries >= config.fifo_max_depth) {
		CPRINTS("KB FIFO depth %d reached",
			config.fifo_max_depth);
		ret = EC_ERROR_OVERFLOW;
		goto kb_fifo_push_done;
	}

	mutex_lock(&fifo_mutex);
	memcpy(kb_fifo[kb_fifo_end], buffp, KEYBOARD_COLS);
	kb_fifo_end = (kb_fifo_end + 1) % KB_FIFO_DEPTH;
	atomic_add(&kb_fifo_entries, 1);
	mutex_unlock(&fifo_mutex);

kb_fifo_push_done:

	if (ret == EC_SUCCESS) {
		set_host_interrupt(1);
#ifdef CONFIG_MKBP_EVENT
		mkbp_send_event(EC_MKBP_EVENT_KEY_MATRIX);
#endif
	}

	return ret;
}

#ifdef CONFIG_MKBP_EVENT
static int keyboard_get_next_event(uint8_t *out)
{
	if (!kb_fifo_entries)
		return -1;

	kb_fifo_remove(out);

	/* Keep sending events if FIFO is not empty */
	if (kb_fifo_entries)
		mkbp_send_event(EC_MKBP_EVENT_KEY_MATRIX);

	return KEYBOARD_COLS;
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_KEY_MATRIX, keyboard_get_next_event);
#endif

void keyboard_send_battery_key(void)
{
	uint8_t state[KEYBOARD_COLS];

	/* Copy debounced state and add battery pseudo-key */
	memcpy(state, keyboard_scan_get_state(), sizeof(state));
	state[BATTERY_KEY_COL] ^= BATTERY_KEY_ROW_MASK;

	/* Add to FIFO only if AP is on or else it will wake from suspend */
	if (chipset_in_state(CHIPSET_STATE_ON))
		keyboard_fifo_add(state);
}

/*****************************************************************************/
/* Host commands */

static int keyboard_get_scan(struct host_cmd_handler_args *args)
{
	kb_fifo_remove(args->response);
	if (!kb_fifo_entries)
		set_host_interrupt(0);

	args->response_size = KEYBOARD_COLS;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_STATE,
		     keyboard_get_scan,
		     EC_VER_MASK(0));

static int keyboard_get_info(struct host_cmd_handler_args *args)
{
	struct ec_response_mkbp_info *r = args->response;

	r->rows = KEYBOARD_ROWS;
	r->cols = KEYBOARD_COLS;
	r->switches = 0;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_MKBP_INFO,
		     keyboard_get_info,
		     EC_VER_MASK(0));

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
		/* Sanity check for fifo depth */
		dst->fifo_max_depth = MIN(src->fifo_max_depth,
					  KB_FIFO_DEPTH);
	}

	new_flags = dst->flags & ~valid_flags;
	new_flags |= src->flags & valid_flags;

	set_keyscan_config(src, dst, valid_mask, new_flags);
	dst->flags = new_flags;
}

static int host_command_mkbp_set_config(struct host_cmd_handler_args *args)
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

static int host_command_mkbp_get_config(struct host_cmd_handler_args *args)
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
