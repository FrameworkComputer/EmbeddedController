/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Physical presence detect state machine
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "physical_presence.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CCD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CCD, format, ## args)

#ifdef CONFIG_PHYSICAL_PRESENCE_DEBUG_UNSAFE
/* More lenient physical presence for dev builds */
#define PP_SHORT_PRESS_COUNT 3
#define PP_SHORT_PRESS_MIN_INTERVAL_US (100 * MSEC)
#define PP_SHORT_PRESS_MAX_INTERVAL_US (15 * SECOND)
#define PP_LONG_PRESS_COUNT (PP_SHORT_PRESS_COUNT + 2)
#define PP_LONG_PRESS_MIN_INTERVAL_US (2 * SECOND)
#define PP_LONG_PRESS_MAX_INTERVAL_US (300 * SECOND)
#else
/* Stricter physical presence for non-dev builds */
#define PP_SHORT_PRESS_COUNT 5
#define PP_SHORT_PRESS_MIN_INTERVAL_US (100 * MSEC)
#define PP_SHORT_PRESS_MAX_INTERVAL_US (5 * SECOND)
#define PP_LONG_PRESS_COUNT (PP_SHORT_PRESS_COUNT + 4)
#define PP_LONG_PRESS_MIN_INTERVAL_US (60 * SECOND)
#define PP_LONG_PRESS_MAX_INTERVAL_US (300 * SECOND)
#endif

enum pp_detect_state {
	PP_DETECT_IDLE = 0,
	PP_DETECT_AWAITING_PRESS,
	PP_DETECT_BETWEEN_PRESSES,
	PP_DETECT_FINISHING,
	PP_DETECT_ABORT
};

/* Physical presence state machine data */
static enum pp_detect_state pp_detect_state;
static void (*pp_detect_callback)(void);
static uint8_t pp_press_count;
static uint8_t pp_press_count_needed;
static uint64_t pp_last_press;  /* Time of last press */

/*
 * We need a mutex because physical_detect_start() and physical_detect_abort()
 * could be called from multiple threads (TPM or console).  And either of those
 * could preempt the deferred functions for the state machine which run in the
 * hook task.
 */
static struct mutex pp_mutex;

static int pp_detect_in_progress(void)
{
	return ((pp_detect_state == PP_DETECT_AWAITING_PRESS) ||
		(pp_detect_state == PP_DETECT_BETWEEN_PRESSES));
}

/******************************************************************************/
/*
 * Deferred functions
 *
 * These are called by the hook task, so can't preempt each other.  But they
 * could be preempted by calls to physical_presence_start() or
 * physical_presence_abort().
 */

/**
 * Clean up at end of physical detect sequence.
 */
static void physical_detect_done(void)
{
	/*
	 * Note that calling physical_detect_abort() from another thread after
	 * the start of physical_detect_done() but before mutex_lock() will
	 * result in another call to physical_detect_done() being queued up.
	 * That's harmless, because we go back to PP_DETECT_IDLE at the end of
	 * this call, so the second call will simply drop through without
	 * calling pp_detect_callback().
	 */
	mutex_lock(&pp_mutex);

	if (!pp_detect_in_progress()) {
		CPRINTF("\nPhysical presence check aborted.\n");
		pp_detect_callback = NULL;
	} else if (pp_press_count < pp_press_count_needed) {
		CPRINTF("\nPhysical presence check timeout.\n");
		pp_detect_callback = NULL;
	}

	pp_detect_state = PP_DETECT_FINISHING;
	mutex_unlock(&pp_mutex);

	/* No longer care about button presses */
	board_physical_presence_enable(0);

	/*
	 * Call the callback function.  Do this outside the mutex, because the
	 * callback may take a while.  If we kept holding the mutex, then calls
	 * to physical_detect_abort() or physical_detect_start() during the
	 * callback would block instead of simply failing.
	 */
	if (pp_detect_callback) {
		CPRINTS("PP callback");
		pp_detect_callback();
		pp_detect_callback = NULL;
	}

	/* Now go to idle */
	mutex_lock(&pp_mutex);
	pp_detect_state = PP_DETECT_IDLE;
	mutex_unlock(&pp_mutex);
}
DECLARE_DEFERRED(physical_detect_done);

/**
 * Print a prompt when we've hit the minimum wait time
 */
static void physical_detect_prompt(void)
{
	pp_detect_state = PP_DETECT_AWAITING_PRESS;
	CPRINTF("\n\nPress the physical button now!\n\n");
}
DECLARE_DEFERRED(physical_detect_prompt);

/**
 * Handle a physical present button press
 *
 * This is implemented as a deferred function so it can use the mutex.
 */
static void physical_detect_check_press(void)
{
	uint64_t now = get_time().val;
	uint64_t dt = now - pp_last_press;

	mutex_lock(&pp_mutex);

	CPRINTS("PP press dt=%.6ld", dt);

	/* If we no longer care about presses, ignore them */
	if (!pp_detect_in_progress())
		goto pdpress_exit;

	/* Ignore extra presses we don't need */
	if (pp_press_count >= pp_press_count_needed)
		goto pdpress_exit;

	/* Ignore presses outside the expected interval */
	if (pp_press_count < PP_SHORT_PRESS_COUNT) {
		if (dt < PP_SHORT_PRESS_MIN_INTERVAL_US) {
			CPRINTS("PP S too soon");
			goto pdpress_exit;
		}
		if (dt > PP_SHORT_PRESS_MAX_INTERVAL_US) {
			CPRINTS("PP S too late");
			goto pdpress_exit;
		}
	} else {
		if (dt < PP_LONG_PRESS_MIN_INTERVAL_US) {
			CPRINTS("PP L too soon");
			goto pdpress_exit;
		}
		if (dt > PP_LONG_PRESS_MAX_INTERVAL_US) {
			CPRINTS("PP L too late");
			goto pdpress_exit;
		}
	}

	/* Ok, we need this press */
	CPRINTS("PP press counted!");
	pp_detect_state = PP_DETECT_BETWEEN_PRESSES;
	pp_last_press = now;
	pp_press_count++;

	/* Set up call to done handler for timeout or actually done */
	if (pp_press_count == pp_press_count_needed) {
		/* Done, so call right away */
		hook_call_deferred(&physical_detect_done_data, 0);
	} else if (pp_press_count < PP_SHORT_PRESS_COUNT) {
		hook_call_deferred(&physical_detect_prompt_data,
				   PP_SHORT_PRESS_MIN_INTERVAL_US);
		hook_call_deferred(&physical_detect_done_data,
				   PP_SHORT_PRESS_MAX_INTERVAL_US);
	} else {
		CPRINTF("Another press will be required soon.\n");
		dt = PP_LONG_PRESS_MAX_INTERVAL_US;
		hook_call_deferred(&physical_detect_prompt_data,
				   PP_LONG_PRESS_MIN_INTERVAL_US);
		hook_call_deferred(&physical_detect_done_data,
				   PP_LONG_PRESS_MAX_INTERVAL_US);
	}

pdpress_exit:
	mutex_unlock(&pp_mutex);
}
DECLARE_DEFERRED(physical_detect_check_press);

/******************************************************************************/
/* Interface */

int physical_detect_start(int is_long, void (*callback)(void))
{
	mutex_lock(&pp_mutex);

	/* Fail if detection is already in progress */
	if (pp_detect_state != PP_DETECT_IDLE) {
		mutex_unlock(&pp_mutex);
		return EC_ERROR_BUSY;
	}

	pp_press_count_needed = is_long ? PP_LONG_PRESS_COUNT :
			PP_SHORT_PRESS_COUNT;
	pp_press_count = 0;
	pp_last_press = get_time().val;
	pp_detect_callback = callback;
	pp_detect_state = PP_DETECT_BETWEEN_PRESSES;
	mutex_unlock(&pp_mutex);

	/* Start capturing button presses */
	hook_call_deferred(&physical_detect_check_press_data, -1);
	board_physical_presence_enable(1);

	CPRINTS("PP start %s", is_long ? "long" : "short");

	/* Initial timeout is for a short press */
	hook_call_deferred(&physical_detect_prompt_data,
			   PP_SHORT_PRESS_MIN_INTERVAL_US);
	hook_call_deferred(&physical_detect_done_data,
			   PP_SHORT_PRESS_MAX_INTERVAL_US);

	return EC_SUCCESS;
}

int physical_detect_busy(void)
{
	return pp_detect_state != PP_DETECT_IDLE;
}

void physical_detect_abort(void)
{
	mutex_lock(&pp_mutex);
	if (pp_detect_in_progress()) {
		CPRINTS("PP abort");
		pp_detect_state = PP_DETECT_ABORT;
		/* Speed up call to done */
		hook_call_deferred(&physical_detect_prompt_data, -1);
		hook_call_deferred(&physical_detect_check_press_data, -1);
		hook_call_deferred(&physical_detect_done_data, 0);
	}
	mutex_unlock(&pp_mutex);
}

int physical_detect_press(void)
{
	/* Ignore presses if we're idle */
	if (pp_detect_state == PP_DETECT_IDLE)
		return EC_ERROR_NOT_HANDLED;

	/* Call the deferred function to do the work */
	hook_call_deferred(&physical_detect_check_press_data, 0);
	return EC_SUCCESS;
}

enum pp_fsm_state physical_presense_fsm_state(void)
{
	switch (pp_detect_state) {
	case PP_DETECT_AWAITING_PRESS:
		return PP_AWAITING_PRESS;
	case PP_DETECT_BETWEEN_PRESSES:
		return PP_BETWEEN_PRESSES;
	default:
		break;
	}

	return PP_OTHER;
}

#ifdef CONFIG_PHYSICAL_PRESENCE_DEBUG_UNSAFE

/**
 * Test callback function
 */
static void pp_test_callback(void)
{
	ccprintf("\nPhysical presence good\n");
}

/**
 * Test physical presence.
 */
static int command_ppresence(int argc, char **argv)
{
	/* Print current status */
	ccprintf("PP state: %d, %d/%d, dt=%.6ld\n",
		 pp_detect_state, pp_press_count, pp_press_count_needed,
		 get_time().val - pp_last_press);

	/* With no args, simulate a button press */
	if (argc < 2) {
		physical_detect_press();
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "short")) {
		return physical_detect_start(0, pp_test_callback);
	} else if (!strcasecmp(argv[1], "long")) {
		return physical_detect_start(1, pp_test_callback);
	} else if (!strcasecmp(argv[1], "abort")) {
		physical_detect_abort();
		return EC_SUCCESS;
	} else {
		return EC_ERROR_PARAM1;
	}
}
DECLARE_SAFE_CONSOLE_COMMAND(ppresence, command_ppresence,
			     "[short | long | abort]",
			     "Test physical presence press or sequence");

#endif
