/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"
#include "panic_utils.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>

/* Helper to get program counter for a thread.
 * Note: For ARM, this reads from the stacked registers assuming standard
 * exception frame layout. For RISC-V, it uses callee-saved registers.
 */
#if defined(CONFIG_ARCH_STACKWALK)
struct pc_lr_data {
	uintptr_t pc;
	uintptr_t lr;
	int frame_idx;
};

static bool get_pc_lr_callback(void *arg, unsigned long pc)
{
	struct pc_lr_data *data = arg;
	if (data->frame_idx == 0) {
		data->pc = pc;
	} else if (data->frame_idx == 1) {
		data->lr = pc;
		return false; /* Stop walking */
	}
	data->frame_idx++;
	return true;
}
#endif

static void get_thread_registers(const struct k_thread *thread, uint32_t *pc,
				 uint32_t *lr)
{
	*pc = 0;
	*lr = 0;

	if (thread == k_current_get() && !k_is_in_isr())
		return;

#if defined(CONFIG_ARCH_STACKWALK)
	struct pc_lr_data data = { .pc = 0, .lr = 0, .frame_idx = 0 };
	arch_stack_walk(get_pc_lr_callback, &data, thread, NULL);
	*pc = (uint32_t)data.pc;
	*lr = (uint32_t)data.lr;
#elif defined(CONFIG_ARM) || defined(CONFIG_RISCV)
	uint32_t sp = get_stack_ptr(thread);
	bool sp_valid = false;

	/* Verify stack pointer is within valid bounds before dereferencing. */
#if defined(CONFIG_THREAD_STACK_INFO)
	uintptr_t stack_start = (uintptr_t)thread->stack_info.start;
	uintptr_t stack_end = stack_start + thread->stack_info.size;
	sp_valid = (sp >= stack_start && sp <= stack_end);
#else
	sp_valid = (sp != 0) && ((sp & 0x3) == 0) && (sp >= CONFIG_RAM_BASE) &&
		   (sp < (CONFIG_RAM_BASE + CONFIG_RAM_SIZE));
#endif

	if (sp_valid) {
		struct arch_esf *esf = (struct arch_esf *)sp;
#if defined(CONFIG_ARM)
		*pc = esf->basic.pc;
		*lr = esf->basic.lr;
#elif defined(CONFIG_RISCV)
		*pc = esf->mepc;
		*lr = esf->ra;
#endif
	}
#endif
}

#ifdef CONFIG_PLATFORM_EC_HOSTCMD_THREAD_INFO

struct list_data {
	struct ec_response_thread_info_list *resp;
	int count;
};

static void match_thread_list(const struct k_thread *thread, void *user_data)
{
	struct list_data *d = user_data;
	/* Limit to EC_THREAD_INFO_MAX_COUNT threads as per
	 * ec_response_thread_info_list fixed array size.
	 */
	if (d->count < EC_THREAD_INFO_MAX_COUNT) {
		d->resp->thread_ids[d->count++] = (uint32_t)(uintptr_t)thread;
	}
}

static enum ec_status hc_thread_info_list(struct host_cmd_handler_args *args)
{
	struct ec_response_thread_info_list *r = args->response;
	struct list_data d = { .resp = r, .count = 0 };

	k_thread_foreach_unlocked(match_thread_list, &d);
	r->thread_count = d.count;
	args->response_size = sizeof(uint32_t) + d.count * sizeof(uint32_t);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THREAD_INFO_LIST, hc_thread_info_list,
		     EC_VER_MASK(0));

extern struct k_thread z_idle_threads[];

/* Checks if a thread is the idle thread. */
static bool thread_is_idle(const struct k_thread *thread)
{
#if defined(CONFIG_SMP)
	return thread->base.is_idle;
#else
	return thread == &z_idle_threads[0];
#endif
}

struct detail_data {
	uintptr_t target_id;
	const struct k_thread *found;
};

static void match_thread_detail(const struct k_thread *thread, void *user_data)
{
	struct detail_data *d = user_data;
	if ((uintptr_t)thread == d->target_id) {
		d->found = thread;
	}
}

static enum ec_status hc_thread_info_detail(struct host_cmd_handler_args *args)
{
	const struct ec_params_thread_info_detail *p = args->params;
	struct ec_response_thread_info_detail *r = args->response;

	struct detail_data d = { .target_id = p->thread_id, .found = NULL };
	k_thread_foreach_unlocked(match_thread_detail, &d);

	if (!d.found) {
		return EC_RES_INVALID_PARAM;
	}

	const struct k_thread *thread = d.found;
	const k_tid_t current = k_current_get();

	memset(r, 0, sizeof(*r));

	r->timestamp_us = k_uptime_get() * 1000;
	r->valid_flags = 0;

#ifdef CONFIG_THREAD_STACK_INFO
	r->valid_flags |= EC_THREAD_INFO_DETAIL_STACK_VALID;
	r->stack_size = thread->stack_info.size;

	uintptr_t cur_sp = get_stack_ptr(thread);
	uintptr_t stack_start = (uintptr_t)thread->stack_info.start;
	uintptr_t stack_end = stack_start + thread->stack_info.size;

	if (cur_sp >= stack_start && cur_sp <= stack_end) {
		r->stack_cur = stack_end - cur_sp;
	} else {
		r->stack_cur = 0;
	}

	size_t unused_stack;
	if (k_thread_stack_space_get(thread, &unused_stack) == 0) {
		r->stack_max = thread->stack_info.size - unused_stack;
	}
#endif /* CONFIG_THREAD_STACK_INFO */

#ifdef CONFIG_SCHED_THREAD_USAGE
	k_thread_runtime_stats_t thread_stats;
	if (k_thread_runtime_stats_get((k_tid_t)thread, &thread_stats) == 0) {
		r->valid_flags |= EC_THREAD_INFO_DETAIL_RUNTIME_USAGE_VALID;
		r->execution_time_us =
			k_cyc_to_us_near64(thread_stats.execution_cycles);

#ifdef CONFIG_SCHED_THREAD_USAGE_ANALYSIS
		r->valid_flags |= EC_THREAD_INFO_DETAIL_USAGE_ANALYSIS_VALID;
		r->window_peak_us =
			k_cyc_to_us_near64(thread_stats.peak_cycles);
		r->window_avg_us =
			k_cyc_to_us_near64(thread_stats.average_cycles);
#endif /* CONFIG_SCHED_THREAD_USAGE_ANALYSIS */
	}
#endif /* CONFIG_SCHED_THREAD_USAGE */

#ifdef CONFIG_THREAD_NAME
	r->valid_flags |= EC_THREAD_INFO_DETAIL_NAME_VALID;
	const char *name = k_thread_name_get((struct k_thread *)thread);
	if (name) {
		strncpy(r->name, name, sizeof(r->name) - 1);
	}
#endif /* CONFIG_THREAD_NAME */

	r->entry_point = (uint32_t)(uintptr_t)thread->entry.pEntry;

	k_ticks_t ticks = k_thread_timeout_remaining_ticks(thread);
	if (ticks > 0 && ticks != K_TICKS_FOREVER) {
		r->timeout_us = k_ticks_to_us_near64(ticks);
	} else if (ticks == K_TICKS_FOREVER) {
		r->timeout_us = 0xffffffff;
	}

	r->user_options = thread->base.user_options;
	r->prio = (int8_t)thread->base.prio;
	r->thread_state = thread->base.thread_state;
	r->is_current = (thread == current);

	if (thread_is_idle(thread)) {
		r->is_idle = 1;
	}

	r->pending_on = (uint32_t)(uintptr_t)thread->base.pended_on;

	/* Fill Arch specific fields. Registers are considered invalid for the
	 * current thread as they cannot be read reliably.
	 */
	if (thread != current) {
		uint32_t pc;
		uint32_t lr;
		get_thread_registers(thread, &pc, &lr);
		uint32_t sp = (uint32_t)get_stack_ptr(thread);

		if (pc != 0) {
			r->pc = pc;
			r->valid_flags |= EC_THREAD_INFO_DETAIL_PC_VALID;
		}
		if (lr != 0) {
			r->lr = lr;
			r->valid_flags |= EC_THREAD_INFO_DETAIL_LR_VALID;
		}
		if (sp != 0) {
			r->sp = sp;
			r->valid_flags |= EC_THREAD_INFO_DETAIL_SP_VALID;
		}
	}

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THREAD_INFO_DETAIL, hc_thread_info_detail,
		     EC_VER_MASK(0));

#endif
