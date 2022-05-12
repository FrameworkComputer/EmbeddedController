/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MKBP keyboard protocol
 */

#include "chipset.h"
#include "common.h"
#include "host_command.h"
#include "keyboard_config.h"
#include "keyboard_mkbp.h"
#include "keyboard_protocol.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "keyboard_test.h"
#include "mkbp_event.h"
#include "mkbp_fifo.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_KEYBOARD, outstr)
#define CPRINTS(format, args...) cprints(CC_KEYBOARD, format, ## args)

/* Changes to col,row here need to also be reflected in kernel.
 * drivers/input/mkbp.c ... see KEY_BATTERY.
 */
#define BATTERY_KEY_COL 0
#define BATTERY_KEY_ROW 7
#define BATTERY_KEY_ROW_MASK BIT(BATTERY_KEY_ROW)

#ifndef HAS_TASK_KEYSCAN
#error "Task KEYSCAN has to be enabled for MKBP keyboard"
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

/*****************************************************************************/
/* Interface */

void keyboard_clear_buffer(void)
{
	mkbp_fifo_clear_keyboard();
}

test_mockable int mkbp_keyboard_add(const uint8_t *buffp)
{
	/*
	 * If the keyboard protocol is not enabled, don't save the state to
	 * the FIFO or trigger an interrupt.
	 */
	if (!(config.flags & EC_MKBP_FLAGS_ENABLE))
		return EC_SUCCESS;

	return mkbp_fifo_add((uint8_t)EC_MKBP_EVENT_KEY_MATRIX, buffp);
}

static int keyboard_get_next_event(uint8_t *out)
{
	return mkbp_fifo_get_next_event(out, EC_MKBP_EVENT_KEY_MATRIX);
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_KEY_MATRIX, keyboard_get_next_event);

void clear_typematic_key(void)
{ }

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

	mkbp_fifo_depth_update(config.fifo_max_depth);

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
