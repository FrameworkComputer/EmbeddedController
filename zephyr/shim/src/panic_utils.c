/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_tasks.h"
#include "panic_utils.h"
#include "uart.h"

#include <stdio.h>

#include <zephyr/kernel.h>

static bool print_trace_address(void *arg, unsigned long pc)
{
	int *frame_idx = (int *)arg;
#ifdef CONFIG_SYMTAB
	uint32_t offset = 0;
	const char *name = symtab_find_symbol_name(pc, &offset);

	printk(" #%d: %p [%s+0x%x]\n", *frame_idx, (void *)pc, name, offset);
#else
	printk(" #%d: %p\n", *frame_idx, (void *)pc);
#endif

	(*frame_idx)++;
	return true;
}

void print_stack_trace(const struct k_thread *thread)
{
	if (!IS_ENABLED(CONFIG_ARCH_STACKWALK)) {
		return;
	}

	if (!uart_init_done()) {
		return;
	}

	int frame_idx = 0;
	char state[32];
	uint32_t sp = 0;
	struct arch_esf *esf = NULL;
	bool is_current_thread = thread == k_current_get();
	char thread_name[16];

	get_thread_name(thread, thread_name, sizeof(thread_name));

	printk("Thread: %s%s, state=%s\n", is_current_thread ? "*" : "",
	       thread_name,
	       k_thread_state_str((k_tid_t)thread, state, sizeof(state)));

	/* Pass esf if this is the currently interrupted thread */
	if (is_current_thread) {
		sp = get_stack_ptr(thread);
		esf = (struct arch_esf *)sp;
	} else {
		esf = NULL;
	}
	arch_stack_walk(print_trace_address, &frame_idx, thread, esf);
}

void get_thread_name(const struct k_thread *thread, char *name, size_t size)
{
#ifdef CONFIG_THREAD_NAME
	snprintf(name, size, "%s", thread->name);
#else
	snprintf(name, size, "TASK_%d", thread_id_to_task_id((k_tid_t)thread));
#endif
}

uint32_t get_stack_ptr(const struct k_thread *thread)
{
#if defined(CONFIG_ARM64)
	/* We are assuming that the SP of interest is SP_EL1 */
	return thread->callee_saved.sp_elx;
#elif defined(CONFIG_ARM)
	return thread->callee_saved.psp;
#elif defined(CONFIG_X86)
#if defined(CONFIG_X86_64)
	return thread->callee_saved.rsp;
#else
	return thread->callee_saved.esp;
#endif
#elif defined(CONFIG_RISCV)
	return thread->callee_saved.sp;
#elif defined(CONFIG_ARCH_POSIX)
	return (uint32_t)thread->callee_saved.thread_status;
#endif
}
