/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "assert.h"
#include "atomic.h"
#include "common.h"
#include "console.h"
#include "limits.h"
#include "math_util.h"
#include "system.h"
#include "usb_pd_timer.h"
#include "usb_tc_sm.h"

#define MAX_PD_PORTS	CONFIG_USB_PD_PORT_MAX_COUNT
#define MAX_PD_TIMERS	PD_TIMER_COUNT
#define PD_TIMERS_ALL_MASK (UINT64_MAX >> (64 - PD_TIMER_COUNT))

#define MAX_EXPIRE	(0x7FFFFFFF)
#define NO_TIMEOUT	(-1)
#define EXPIRE_NOW	(0)

#define PD_SET_ACTIVE(p, m)	pd_timer_atomic_op(		\
					atomic_or,		\
					timer_active[p],	\
					(m))
#define PD_CLR_ACTIVE(p, m)	pd_timer_atomic_op(		\
					atomic_clear_bits,	\
					timer_active[p],	\
					(m))
#define PD_CHK_ACTIVE(p, m)	((timer_active[p][0] & ((m) >> 32)) | \
				 (timer_active[p][1] & (m)))

#define PD_SET_DISABLED(p, m)	pd_timer_atomic_op(		\
					atomic_or,		\
					timer_disabled[p],	\
					(m))
#define PD_CLR_DISABLED(p, m)	pd_timer_atomic_op(		\
					atomic_clear_bits,	\
					timer_disabled[p],	\
					(m))
#define PD_CHK_DISABLED(p, m)	((timer_disabled[p][0] & ((m) >> 32)) | \
				 (timer_disabled[p][1] & (m)))

#define TIMER_FIELD_NUM_UINT32S 2

test_mockable_static
uint32_t timer_active[MAX_PD_PORTS][TIMER_FIELD_NUM_UINT32S];
test_mockable_static
uint32_t timer_disabled[MAX_PD_PORTS][TIMER_FIELD_NUM_UINT32S];
static uint64_t timer_expires[MAX_PD_PORTS][MAX_PD_TIMERS];
BUILD_ASSERT(sizeof(timer_active[0]) * CHAR_BIT >= PD_TIMER_COUNT);
BUILD_ASSERT(sizeof(timer_disabled[0]) * CHAR_BIT >= PD_TIMER_COUNT);

/*
 * CONFIG_CMD_PD_TIMER debug variables
 */
static int count[MAX_PD_PORTS];
static int max_count[MAX_PD_PORTS];

__maybe_unused static __const_data const char * const pd_timer_names[] = {
	[PE_TIMER_BIST_CONT_MODE]	= "PE-BIST_CONT_MODE",
	[PE_TIMER_CHUNKING_NOT_SUPPORTED] = "PE-CHUNKING_NOT_SUPPORTED",
	[PE_TIMER_DISCOVER_IDENTITY]	= "PE-DISCOVER_IDENTITY",
	[PE_TIMER_NO_RESPONSE]		= "PE-NO_RESPONSE",
	[PE_TIMER_PR_SWAP_WAIT]		= "PE-PR_SWAP_WAIT",
	[PE_TIMER_PS_HARD_RESET]	= "PE-PS_HARD_RESET",
	[PE_TIMER_PS_SOURCE]		= "PE-PS_SOURCE",
	[PE_TIMER_PS_TRANSITION]	= "PE-PS_TRANSITION",
	[PE_TIMER_SENDER_RESPONSE]	= "PE-SENDER_RESPONSE",
	[PE_TIMER_SINK_REQUEST]		= "PE-SINK_REQUEST",
	[PE_TIMER_SOURCE_CAP]		= "PE-SOURCE_CAP",
	[PE_TIMER_SRC_TRANSITION]	= "PE-SRC_TRANSITION",
	[PE_TIMER_SWAP_SOURCE_START]	= "PE-SWAP_SOURCE_START",
	[PE_TIMER_TIMEOUT]		= "PE-TIMEOUT",
	[PE_TIMER_VCONN_ON]		= "PE-VCONN_ON",
	[PE_TIMER_VDM_RESPONSE]		= "PE-VDM_RESPONSE",
	[PE_TIMER_WAIT_AND_ADD_JITTER]	= "PE-WAIT_AND_ADD_JITTER",

	[PR_TIMER_CHUNK_SENDER_REQUEST]	= "PR-CHUNK_SENDER_REQUEST",
	[PR_TIMER_CHUNK_SENDER_RESPONSE] = "PR-CHUNK_SENDER_RESPONSE",
	[PR_TIMER_HARD_RESET_COMPLETE]	= "PR-HARD_RESET_COMPLETE",
	[PR_TIMER_SINK_TX]		= "PR-SINK_TX",
	[PR_TIMER_TCPC_TX_TIMEOUT]	= "PR-TCPC_TX_TIMEOUT",

	[TC_TIMER_CC_DEBOUNCE]		= "TC-CC_DEBOUNCE",
	[TC_TIMER_LOW_POWER_EXIT_TIME]	= "TC-LOW_POWER_EXIT_TIME",
	[TC_TIMER_LOW_POWER_TIME]	= "TC-LOW_POWER_TIME",
	[TC_TIMER_NEXT_ROLE_SWAP]	= "TC-NEXT_ROLE_SWAP",
	[TC_TIMER_PD_DEBOUNCE]		= "TC-PD_DEBOUNCE",
	[TC_TIMER_TIMEOUT]		= "TC-TIMEOUT",
	[TC_TIMER_TRY_WAIT_DEBOUNCE]	= "TC-TRY_WAIT_DEBOUNCE",
	[TC_TIMER_VBUS_DEBOUNCE]	= "TC-VBUS_DEBOUNCE",
};

/*****************************************************************************
 * PD_TIMER private functions
 *
 * The view of timers to the outside world is enabled and disabled. Internally
 * timers that are enabled are in the active and inactive states. An active
 * timer has a valid timeout value that gets checked for expiration and can
 * adjust the task wakeup time.  An inactive timer is assumed to have expired
 * already and will always return that it is still expired.  This timer state
 * will not adjust the task scheduling timeout value.
 */

/*
 * Performs an atomic operation on a PD timer bit field. Atomic operations
 * require 32-bit operands, but there are more than 32 timers, so choose the
 * correct operand and modify the mask accordingly.
 *
 * @param op          Atomic operation function to call
 * @param timer_field Array of timer fields to operate on
 * @param mask_val    64-bit mask to apply to the timer field
 */
test_mockable_static void pd_timer_atomic_op(
		atomic_val_t (*op)(atomic_t*, atomic_val_t),
		uint32_t *const timer_field, const uint64_t mask_val)
{
	uint32_t *atomic_timer_field;
	union mask64_t {
		struct {
#if (__BYTE_ORDER__  == __ORDER_LITTLE_ENDIAN__)
			uint32_t lo;
			uint32_t hi;
#elif (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
			uint32_t hi;
			uint32_t lo;
#endif
		};
		uint64_t val;
	} mask;

	/*
	 * High-order mask bits correspond to field [0]. Low-order mask bits
	 * correspond to field [1].
	 */
	mask.val = mask_val;
	if (mask.hi) {
		atomic_timer_field = timer_field;
		(void)op(atomic_timer_field, mask.hi);
	}
	if (mask.lo) {
		atomic_timer_field = timer_field + 1;
		(void)op(atomic_timer_field, mask.lo);
	}
}

static void pd_timer_inactive(int port, enum pd_task_timer timer)
{
	uint64_t mask = bitmask_uint64(timer);

	if (PD_CHK_ACTIVE(port, mask)) {
		PD_CLR_ACTIVE(port, mask);

		if (IS_ENABLED(CONFIG_CMD_PD_TIMER))
			count[port]--;
	}
	PD_CLR_DISABLED(port, mask);
}

static bool pd_timer_is_active(int port, enum pd_task_timer timer)
{
	uint64_t mask = bitmask_uint64(timer);

	return PD_CHK_ACTIVE(port, mask);
}

static bool pd_timer_is_inactive(int port, enum pd_task_timer timer)
{
	uint64_t mask = bitmask_uint64(timer);

	return !PD_CHK_ACTIVE(port, mask) && !PD_CHK_DISABLED(port, mask);
}

/*****************************************************************************
 * PD_TIMER public functions
 */
void pd_timer_init(int port)
{
	if (IS_ENABLED(CONFIG_CMD_PD_TIMER))
		count[port] = 0;

	PD_CLR_ACTIVE(port, PD_TIMERS_ALL_MASK);
	PD_SET_DISABLED(port, PD_TIMERS_ALL_MASK);
}

void pd_timer_enable(int port, enum pd_task_timer timer, uint32_t expires_us)
{
	uint64_t mask = bitmask_uint64(timer);

	if (!PD_CHK_ACTIVE(port, mask)) {
		PD_SET_ACTIVE(port, mask);

		if (IS_ENABLED(CONFIG_CMD_PD_TIMER)) {
			count[port]++;
			if (count[port] > max_count[port])
				max_count[port] = count[port];
		}
	}
	PD_CLR_DISABLED(port, mask);
	timer_expires[port][timer] = get_time().val + expires_us;
}

void pd_timer_disable(int port, enum pd_task_timer timer)
{
	uint64_t mask = bitmask_uint64(timer);

	if (PD_CHK_ACTIVE(port, mask)) {
		PD_CLR_ACTIVE(port, mask);

		if (IS_ENABLED(CONFIG_CMD_PD_TIMER))
			count[port]--;
	}
	PD_SET_DISABLED(port, mask);
}

void pd_timer_disable_range(int port, enum pd_timer_range range)
{
	int start, end;
	enum pd_task_timer timer;

	switch (range) {
	case PE_TIMER_RANGE:
		start	= PE_TIMER_START;
		end	= PE_TIMER_END;
		break;
	case PR_TIMER_RANGE:
		start	= PR_TIMER_START;
		end	= PR_TIMER_END;
		break;
	case TC_TIMER_RANGE:
		start	= TC_TIMER_START;
		end	= TC_TIMER_END;
		break;
	default:
		return;
	}

	for (timer = start; timer <= end; ++timer)
		pd_timer_disable(port, timer);
}

bool pd_timer_is_disabled(int port, enum pd_task_timer timer)
{
	uint64_t mask = bitmask_uint64(timer);

	return PD_CHK_DISABLED(port, mask);
}

bool pd_timer_is_expired(int port, enum pd_task_timer timer)
{
	if (pd_timer_is_active(port, timer)) {
		if (get_time().val >= timer_expires[port][timer]) {
			pd_timer_inactive(port, timer);
			return true;
		}
		return false;
	}
	return pd_timer_is_inactive(port, timer);
}

void pd_timer_manage_expired(int port)
{
	int timer;

	if (timer_active[port])
		for (timer = 0; timer < MAX_PD_TIMERS; ++timer)
			if (pd_timer_is_active(port, timer) &&
			    pd_timer_is_expired(port, timer))
				pd_timer_inactive(port, timer);
}

int pd_timer_next_expiration(int port)
{
	int timer;
	int ret_value = MAX_EXPIRE;
	uint64_t now = get_time().val;

	for (timer = 0; timer < MAX_PD_TIMERS; ++timer) {
		/* Only use active timers for the next expired value */
		if (pd_timer_is_active(port, timer)) {
			int delta;
			uint64_t t_value = timer_expires[port][timer];

			if (t_value <= now) {
				ret_value = EXPIRE_NOW;
				break;
			}

			delta = t_value - now;
			if (ret_value > delta)
				ret_value = delta;
		}
	}

	if (ret_value == MAX_EXPIRE)
		ret_value = NO_TIMEOUT;

	return ret_value;
}

#ifdef CONFIG_CMD_PD_TIMER
void pd_timer_dump(int port)
{
	int timer;
	uint64_t now = get_time().val;

	ccprints("Timers(%d): cur=%d max=%d",
		port, count[port], max_count[port]);

	for (timer = 0; timer < MAX_PD_TIMERS; ++timer) {
		if (pd_timer_is_disabled(port, timer)) {
			continue;
		} else if (pd_timer_is_active(port, timer)) {
			uint32_t delta = 0;

			if (now < timer_expires[port][timer])
				delta = timer_expires[port][timer] - now;

			ccprints("[%2d] Active:   %s (%d%s)",
				timer, pd_timer_names[timer], (uint32_t)delta,
				tc_event_loop_is_paused(port)
					? "-PAUSED"
					: "");
		} else {
			ccprints("[%2d] Inactive: %s",
				timer, pd_timer_names[timer]);
		}
	}
}
#endif /* CONFIG_CMD_PD_TIMER */
