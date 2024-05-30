/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "common.h"
#include "console.h"
#include "stdbool.h"
#include "task.h"
#line 17
#include "usb_pd.h"
#include "usb_sm.h"
#include "util.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USB, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USB, format, ##args)
#else /* CONFIG_COMMON_RUNTIME */
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/* Private structure (to this file) used to track state machine context */
struct internal_ctx {
	usb_state_ptr last_entered;
	uint32_t running : 1;
	uint32_t enter : 1;
	uint32_t exit : 1;
};
BUILD_ASSERT(sizeof(struct internal_ctx) ==
	     member_size(struct sm_ctx, internal));

/* Gets the first shared parent state between a and b (inclusive) */
static usb_state_ptr shared_parent_state(usb_state_ptr a, usb_state_ptr b)
{
	const usb_state_ptr orig_b = b;

	/* There are no common ancestors */
	if (b == NULL)
		return NULL;

	/* This assumes that both A and B are NULL terminated without cycles */
	while (a != NULL) {
		/* We found a match return */
		if (a == b)
			return a;

		/*
		 * Otherwise, increment b down the list for comparison until we
		 * run out, then increment a and start over on b for comparison
		 */
		if (b->parent == NULL) {
			a = a->parent;
			b = orig_b;
		} else {
			b = b->parent;
		}
	}

	return NULL;
}

/*
 * Call all entry functions of parents before children. If set_state is called
 * during one of the entry functions, then do not call any remaining entry
 * functions.
 */
static void call_entry_functions(const int port,
				 struct internal_ctx *const internal,
				 const usb_state_ptr stop,
				 const usb_state_ptr current)
{
	if (current == stop)
		return;

	call_entry_functions(port, internal, stop, current->parent);

	/*
	 * If the previous entry function called set_state, then don't enter
	 * remaining states.
	 */
	if (!internal->enter)
		return;

	/* Track the latest state that was entered, so we can exit properly. */
	internal->last_entered = current;
	if (current->entry)
		current->entry(port);
}

/*
 * Call all exit functions of children before parents. Note set_state is ignored
 * during an exit function.
 */
static void call_exit_functions(const int port, const usb_state_ptr stop,
				const usb_state_ptr current)
{
	if (current == stop)
		return;

	if (current->exit)
		current->exit(port);

	call_exit_functions(port, stop, current->parent);
}

void set_state(const int port, struct sm_ctx *const ctx,
	       const usb_state_ptr new_state)
{
	struct internal_ctx *const internal = (void *)ctx->internal;
	usb_state_ptr last_state;
	usb_state_ptr shared_parent;

	/*
	 * It does not make sense to call set_state in an exit phase of a state
	 * since we are already in a transition; we would always ignore the
	 * intended state to transition into.
	 */
	if (internal->exit) {
		CPRINTF("C%d: Ignoring set state to 0x%p within 0x%p", port,
			new_state, ctx->current);
		return;
	}

	/*
	 * Determine the last state that was entered. Normally it is current,
	 * but we could have called set_state within an entry phase, so we
	 * shouldn't exit any states that weren't fully entered.
	 */
	last_state = internal->enter ? internal->last_entered : ctx->current;

	/* We don't exit and re-enter shared parent states */
	shared_parent = shared_parent_state(last_state, new_state);

	/*
	 * Exit all of the non-common states from the last state.
	 */
	internal->exit = true;
	call_exit_functions(port, shared_parent, last_state);
	internal->exit = false;

	ctx->previous = ctx->current;
	ctx->current = new_state;

	/*
	 * Enter all new non-common states. last_entered will contain the last
	 * state that successfully entered before another set_state was called.
	 */
	internal->last_entered = NULL;
	internal->enter = true;
	call_entry_functions(port, internal, shared_parent, ctx->current);
	/*
	 * Setting enter to false ensures that all pending entry calls will be
	 * skipped (in the case of a parent state calling set_state, which means
	 * we should not enter any child states)
	 */
	internal->enter = false;

	/*
	 * If we set_state while we are running a child state, then stop running
	 * any remaining parent states.
	 */
	internal->running = false;

	/*
	 * Since we are changing states, we want to ensure that we process the
	 * next state's run method as soon as we can to ensure that we don't
	 * delay important processing until the next task interval.
	 */
	if (IS_ENABLED(HAS_TASK_PD_C0))
		task_wake(PD_PORT_TO_TASK_ID(port));
}

/*
 * Call all run functions of children before parents. If set_state is called
 * during one of the entry functions, then do not call any remaining entry
 * functions.
 */
static void call_run_functions(const int port,
			       const struct internal_ctx *const internal,
			       const usb_state_ptr current)
{
	if (!current)
		return;

	/* If set_state is called during run, don't call remain functions. */
	if (!internal->running)
		return;

	if (current->run)
		current->run(port);

	call_run_functions(port, internal, current->parent);
}

void run_state(const int port, struct sm_ctx *const ctx)
{
	struct internal_ctx *const internal = (void *)ctx->internal;

	internal->running = true;
	call_run_functions(port, internal, ctx->current);
	internal->running = false;
}
