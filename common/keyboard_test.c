/*
 * Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <common.h>
#include <console.h>
#include <ec_commands.h>
#include <host_command.h>
#include <keyboard_test.h>
#include <task.h>
#include <util.h>

enum {
	KEYSCAN_MAX_LENGTH		= 20,
	KEYSCAN_SEQ_START_DELAY_US	= 10000,
};

static uint8_t keyscan_seq_count;
static int8_t keyscan_seq_upto = -1;
static struct keyscan_item keyscan_items[KEYSCAN_MAX_LENGTH];
struct keyscan_item *keyscan_seq_cur;

static int keyscan_seq_is_active(void)
{
	return keyscan_seq_upto != -1;
}

/**
 * Get the current item in the keyscan sequence
 *
 * This looks at the current time, and returns the correct key scan for that
 * time.
 *
 * @return pointer to keyscan item, or NULL if none
 */
static const struct keyscan_item *keyscan_seq_get(void)
{
	struct keyscan_item *ksi;

	if (!keyscan_seq_is_active())
		return NULL;

	ksi = &keyscan_items[keyscan_seq_upto];
	while (keyscan_seq_upto < keyscan_seq_count) {
		/*
		 * If we haven't reached the time for the next one, return
		 * this one.
		 */
		if (!timestamp_expired(ksi->abs_time, NULL)) {
			/* Yippee, we get to present this one! */
			if (keyscan_seq_cur)
				keyscan_seq_cur->done = 1;
			return keyscan_seq_cur;
		}

		keyscan_seq_cur = ksi;
		keyscan_seq_upto++;
		ksi++;
	}

	ccprints("keyscan_seq done, upto=%d", keyscan_seq_upto);
	keyscan_seq_upto = -1;
	keyscan_seq_cur = NULL;
	return NULL;
}

uint8_t keyscan_seq_get_scan(int column, uint8_t scan)
{
	const struct keyscan_item *item;

	/* Use simulated keyscan sequence instead if active */
	item = keyscan_seq_get();
	if (item) {
		/* OR all columns together */
		if (column == -1) {
			int c;

			scan = 0;
			for (c = 0; c < keyboard_cols; c++)
				scan |= item->scan[c];
		} else {
			scan = item->scan[column];
		}
	}

	return scan;
}

int keyscan_seq_next_event_delay(void)
{
	const struct keyscan_item *ksi;
	int delay;

	/*
	 * Make sure we are pointing to the right event. This function will
	 * return the event that should currently be presented. In fact we
	 * want to look at the next event to be presented, so we manually
	 * look that up after calling this function.
	 */
	ksi = keyscan_seq_get();

	if (!keyscan_seq_is_active())
		return -1;

	/* Calculate the delay until the event */
	ksi = &keyscan_items[keyscan_seq_upto];
	delay = MAX(ksi->abs_time.val - get_time().val, 0);

	return delay;
}

static void keyscan_seq_start(void)
{
	timestamp_t start;
	int i;

	start = get_time();
	start.val += KEYSCAN_SEQ_START_DELAY_US;
	for (i = 0; i < keyscan_seq_count; i++) {
		struct keyscan_item *ksi = &keyscan_items[i];

		ksi->abs_time = start;
		ksi->abs_time.val += ksi->time_us;
	}

	keyscan_seq_upto = 0;
	keyscan_seq_cur = NULL;
	task_wake(TASK_ID_KEYSCAN);
}

static int keyscan_seq_collect(struct ec_params_keyscan_seq_ctrl *req,
			       struct ec_result_keyscan_seq_ctrl *resp)
{
	struct keyscan_item *ksi;
	int start, end;
	int i;

	/* Range check the input values */
	start = req->collect.start_item;
	end = start + req->collect.num_items;
	if (start >= keyscan_seq_count)
		end = start;
	else
		end = MIN(end, keyscan_seq_count);
	start = MIN(start, end);

	/* Response plus one byte per item */
	end = MIN(end - start, EC_HOST_PARAM_SIZE - sizeof(*resp));
	resp->collect.num_items = end - start;

	for (i = start, ksi = keyscan_items; i < end; i++, ksi++)
		resp->collect.item[i].flags = ksi->done ?
						EC_KEYSCAN_SEQ_FLAG_DONE : 0;

	return sizeof(*resp) + resp->collect.num_items;
}

static enum ec_status keyscan_seq_ctrl(struct host_cmd_handler_args *args)
{
	struct ec_params_keyscan_seq_ctrl req, *msg;
	struct keyscan_item *ksi;

	/* For now we must do our own alignment */
	memcpy(&req, args->params, sizeof(req));

	ccprintf("keyscan %d\n", req.cmd);
	switch (req.cmd) {
	case EC_KEYSCAN_SEQ_CLEAR:
		keyscan_seq_count = 0;
		break;
	case EC_KEYSCAN_SEQ_ADD:
		if (keyscan_seq_count == KEYSCAN_MAX_LENGTH)
			return EC_RES_OVERFLOW;

		ksi = &keyscan_items[keyscan_seq_count];
		ksi->time_us = req.add.time_us;
		ksi->done = 0;
		ksi->abs_time.val = 0;
		msg = (struct ec_params_keyscan_seq_ctrl *)args->params;
		memcpy(ksi->scan, msg->add.scan, sizeof(ksi->scan));
		keyscan_seq_count++;
		break;
	case EC_KEYSCAN_SEQ_START:
		keyscan_seq_start();
		break;
	case EC_KEYSCAN_SEQ_COLLECT:
		args->response_size = keyscan_seq_collect(&req,
			(struct ec_result_keyscan_seq_ctrl *)args->response);
		break;
	default:
		return EC_RES_INVALID_COMMAND;
	}

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_KEYSCAN_SEQ_CTRL,
		     keyscan_seq_ctrl,
		     EC_VER_MASK(0));
