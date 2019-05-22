/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "task.h"
#include "usb_pd.h"
#include "usb_sm.h"
#include "util.h"
#include "console.h"

void init_state(int port, struct sm_obj *obj, sm_state target)
{
#if (CONFIG_SM_NESTING_NUM > 0)
	int i;

	sm_state tmp_super[CONFIG_SM_NESTING_NUM];
#endif

	obj->last_state = NULL;
	obj->task_state = target;

#if (CONFIG_SM_NESTING_NUM > 0)

	/* Prepare to execute all entry actions of the target's super states */

	/*
	 * Get targets super state. This will be NULL if the target
	 * has no super state
	 */
	tmp_super[CONFIG_SM_NESTING_NUM - 1] =
			(sm_state)(uintptr_t)target(port, SUPER_SIG);

	/* Get all super states of the target */
	for (i = CONFIG_SM_NESTING_NUM - 1; i > 0; i--) {
		if (tmp_super[i] != NULL)
			tmp_super[i - 1] =
			(sm_state)(uintptr_t)tmp_super[i](port, SUPER_SIG);
		else
			tmp_super[i - 1] = NULL;
	}

	/* Execute all super state entry actions in forward order */
	for (i = 0; i < CONFIG_SM_NESTING_NUM; i++)
		if (tmp_super[i] != NULL)
			tmp_super[i](port, ENTRY_SIG);
#endif

	/* Now execute the target entry action */
	target(port, ENTRY_SIG);
}

int set_state(int port, struct sm_obj *obj, sm_state target)
{
#if (CONFIG_SM_NESTING_NUM > 0)
	int i;
	int no_execute;

	sm_state tmp_super[CONFIG_SM_NESTING_NUM];
	sm_state target_super;
	sm_state last_super;
	sm_state super;

	/* Execute all exit actions is reverse order */

	/* Get target's super state */
	target_super = (sm_state)(uintptr_t)target(port, SUPER_SIG);
	tmp_super[0] = obj->task_state;

	do {
		/* Execute exit action */
		tmp_super[0](port, EXIT_SIG);

		/* Get super state */
		tmp_super[0] =
			(sm_state)(uintptr_t)tmp_super[0](port, SUPER_SIG);
		/*
		 * No need to execute a super state's exit action that has
		 * shared ancestry with the target.
		 */
		super = target_super;
		while (super != NULL) {
			if (tmp_super[0] == super) {
				tmp_super[0] = NULL;
				break;
			}

			/* Get target state next super state if it exists */
			super = (sm_state)(uintptr_t)super(port, SUPER_SIG);
		}
	} while (tmp_super[0] != NULL);

	/* All done executing the exit actions */
#else
	obj->task_state(port, EXIT_SIG);
#endif
	/* update the state variables */
	obj->last_state = obj->task_state;
	obj->task_state = target;

#if (CONFIG_SM_NESTING_NUM > 0)
	/* Prepare to execute all entry actions of the target's super states */

	tmp_super[CONFIG_SM_NESTING_NUM - 1] =
				(sm_state)(uintptr_t)target(port, SUPER_SIG);

	/* Get all super states of the target */
	for (i = CONFIG_SM_NESTING_NUM - 1; i > 0; i--) {
		if (tmp_super[i] != NULL)
			tmp_super[i - 1] =
			(sm_state)(uintptr_t)tmp_super[i](port, SUPER_SIG);
		else
			tmp_super[i - 1] = NULL;
	}

	/* Get super state of last state */
	last_super = (sm_state)(uintptr_t)obj->last_state(port, SUPER_SIG);

	/* Execute all super state entry actions in forward order */
	for (i = 0; i < CONFIG_SM_NESTING_NUM; i++) {
		/* No super state */
		if (tmp_super[i] == NULL)
			continue;

		/*
		 * We only want to execute the target state's super state entry
		 * action if it doesn't share a super state with the previous
		 * state.
		 */
		super = last_super;
		no_execute = 0;
		while (super != NULL) {
			if (tmp_super[i] == super) {
				no_execute = 1;
				break;
			}

			/* Get last state's next super state if it exists */
			super = (sm_state)(uintptr_t)super(port, SUPER_SIG);
		}

		/* Execute super state's entry */
		if (!no_execute)
			tmp_super[i](port, ENTRY_SIG);
	}
#endif

	/* Now execute the target entry action */
	target(port, ENTRY_SIG);

	return 0;
}

void exe_state(int port, struct sm_obj *obj, enum signal sig)
{
#if (CONFIG_SM_NESTING_NUM > 0)
	sm_state state = obj->task_state;

	do {
		state = (sm_state)(uintptr_t)state(port, sig);
	} while (state != NULL);
#else
	obj->task_state(port, sig);
#endif
}

unsigned int do_nothing_exit(int port)
{
	return 0;
}

unsigned int get_super_state(int port)
{
	return RUN_SUPER;
}

