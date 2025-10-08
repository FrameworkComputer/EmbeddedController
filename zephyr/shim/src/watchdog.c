/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "ec_tasks.h"
#include "hooks.h"
#include "panic.h"
#include "task.h"
#include "watchdog.h"

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(watchdog_shim, LOG_LEVEL_ERR);

#ifdef CONFIG_PLATFORM_EC_WATCHDOG_DUMP_THREADS_CALL_STACK
BUILD_ASSERT(CONFIG_AUX_TIMER_PERIOD_MS >= 500,
	     "Make sure we have enough lead time to dump call stacks");
#endif

struct watchdog_info {
	const struct device *wdt_dev;
	struct wdt_timeout_cfg config;
};

__maybe_unused static void wdt_warning_handler(const struct device *wdt_dev,
					       int channel_id);
__maybe_unused static void
wdt_warning_handler_with_enable(const struct device *wdt_dev, int channel_id);

const struct watchdog_info wdt_info[] = {
#if DT_NODE_HAS_STATUS(DT_CHOSEN(cros_ec_watchdog), okay)
	{
		.wdt_dev = DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog)),
		.config = {
#if ((DT_NODE_HAS_COMPAT(DT_CHOSEN(cros_ec_watchdog), st_stm32_watchdog)) || \
     (DT_NODE_HAS_COMPAT(DT_CHOSEN(cros_ec_watchdog),                        \
			 realtek_rts5912_watchdog)))
			.flags = WDT_FLAG_RESET_SOC,
			.window.min = 0U,
			.window.max = CONFIG_WATCHDOG_PERIOD_MS,
			.callback = NULL,
#else
			.flags = WDT_FLAG_RESET_SOC,
			.window.min = 0U,
			.window.max = CONFIG_AUX_TIMER_PERIOD_MS,
			.callback = wdt_warning_handler,
#endif
		},
	},
#endif
#ifdef CONFIG_PLATFORM_EC_WATCHDOG_HELPER
	{
		.wdt_dev = DEVICE_DT_GET(DT_CHOSEN(cros_ec_watchdog_helper)),
		.config = {
			.flags = 0U,
			.window.min = 0U,
			.window.max = CONFIG_AUX_TIMER_PERIOD_MS,
			.callback = wdt_warning_handler_with_enable,
		},
	},
#endif
};

/* Array to keep channel used to implement watchdog */
int wdt_chan[ARRAY_SIZE(wdt_info)];
bool watchdog_initialized;

#ifdef TEST_BUILD
bool wdt_warning_triggered;
#endif /* TEST_BUILD */

static int watchdog_config(const struct watchdog_info *info)
{
	const struct device *wdt_dev = info->wdt_dev;
	const struct wdt_timeout_cfg *config = &info->config;
	int chan;

	chan = wdt_install_timeout(wdt_dev, config);

	/* If watchdog is running, reinstall it. */
	if (chan == -EBUSY) {
		wdt_disable(wdt_dev);
		chan = wdt_install_timeout(wdt_dev, config);
	}

	if (chan < 0) {
		LOG_ERR("Watchdog install error: %d", chan);
	}

	return chan;
}

static int watchdog_enable(const struct device *wdt_dev)
{
	int err;

	err = wdt_setup(wdt_dev, 0);
	if (err < 0)
		LOG_ERR("Watchdog %s setup error: %d", wdt_dev->name, err);

	return err;
}

static int watchdog_init_device(const struct watchdog_info *info)
{
	const struct device *wdt_dev = info->wdt_dev;
	int chan, err;

	if (!device_is_ready(wdt_dev)) {
		LOG_ERR("device %s not ready", wdt_dev->name);
		return -ENODEV;
	}

	chan = watchdog_config(info);
	if (chan < 0)
		return chan;

	err = watchdog_enable(wdt_dev);
	if (err < 0)
		return err;

	return chan;
}

int watchdog_init(void)
{
	int err = EC_SUCCESS;

	if (watchdog_initialized)
		return -EBUSY;

	for (int i = 0; i < ARRAY_SIZE(wdt_info); i++) {
		wdt_chan[i] = watchdog_init_device(&wdt_info[i]);
		if (wdt_chan[i] < 0 && err == EC_SUCCESS)
			err = wdt_chan[i];
	}

	watchdog_initialized = true;
	watchdog_reload();

	return err;
}

void watchdog_reload(void)
{
	if (!watchdog_initialized)
		return;

	for (int i = 0; i < ARRAY_SIZE(wdt_info); i++) {
		if (wdt_chan[i] < 0)
			continue;

		wdt_feed(wdt_info[i].wdt_dev, wdt_chan[i]);
	}
}
DECLARE_HOOK(HOOK_TICK, watchdog_reload, HOOK_PRIO_DEFAULT);

static void get_thread_name(const struct k_thread *thread, char *name,
			    size_t size)
{
#ifdef CONFIG_THREAD_NAME
	snprintf(name, size, "%s", thread->name);
#else
	snprintf(name, size, "TASK_%d", thread_id_to_task_id((k_tid_t)thread));
#endif
}

static uint32_t get_stack_ptr(const struct k_thread *thread)
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

static void print_sp_pc(const struct k_thread *thread)
{
	uint32_t sp = get_stack_ptr(thread);
	struct arch_esf *esf = (struct arch_esf *)sp;
	char thread_name[16];

	get_thread_name(thread, thread_name, sizeof(thread_name));

#if defined(CONFIG_ARM)
	printk("%s [SP=%p, PC=%p, LR=%p]\n", thread_name, (void *)sp,
	       (void *)esf->basic.pc, (void *)esf->basic.lr);
#elif defined(CONFIG_X86)
#if defined(CONFIG_X86_64)
	printk("%s [SP=%p, PC=%p]\n", thread_name, (void *)sp,
	       (void *)esf->rip);
#else
	printk("%s [SP=%p, PC=%p]\n", thread_name, (void *)sp,
	       (void *)esf->eip);
#endif
#elif defined(CONFIG_RISCV)
	printk("%s [SP=%p, PC=%p, RA=%p]\n", thread_name, (void *)sp,
	       (void *)esf->mepc, (void *)esf->ra);
#elif defined(CONFIG_ARCH_POSIX)
	/* Nothing useful within esf to be printed here */
	ARG_UNUSED(esf);
#endif
}

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

static void print_stack_trace(const struct k_thread *thread)
{
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

static void log_thread_info(const struct k_thread *thread, void *user_data)
{
	print_sp_pc(thread);
	if (IS_ENABLED(CONFIG_PLATFORM_EC_WATCHDOG_DUMP_THREADS_CALL_STACK)) {
		print_stack_trace(thread);
	}
}

__maybe_unused static void wdt_warning_handler(const struct device *wdt_dev,
					       int channel_id)
{
	uint32_t exception_address = 0;
	const char *thread_name;
	if (IS_ENABLED(CONFIG_THREAD_NAME)) {
		thread_name = k_thread_name_get(k_current_get());
	} else {
		thread_name = "unknown";
	}
	task_id_t task_id = task_get_current();

#ifdef CONFIG_RISCV
	exception_address = csr_read(mepc);
	printk("WDT pre-warning MEPC:%p TASK_ID:%d THREAD_NAME:%s\n",
	       (void *)exception_address, task_id, thread_name);
#elif CONFIG_CPU_CORTEX_M
	struct arch_esf *esf;
	/*
	 * Watchdog warning should only be triggered while executing in thread
	 * context, thus PSP will point to esf.
	 */
	__asm__ volatile("mrs %0, psp" : "=r"(esf));
	printk("WDT pre-warning PC:%p LR:%p TASK_ID:%d THREAD_NAME:%s\n",
	       (void *)esf->basic.pc, (void *)esf->basic.lr, task_id,
	       thread_name);
	exception_address = esf->basic.pc;
#else
	/* TODO(b/176523207): watchdog warning message */
	printk("Watchdog deadline is close! TASK_ID:%d THREAD_NAME:%s\n",
	       task_id, thread_name);
#endif
#ifdef TEST_BUILD
	wdt_warning_triggered = true;
#endif
#ifdef CONFIG_SOC_SERIES_MEC172X
	extern void cros_chip_wdt_handler(const struct device *wdt_dev,
					  int channel_id);
	cros_chip_wdt_handler(wdt_dev, channel_id);
#endif

	if (IS_ENABLED(CONFIG_THREAD_MONITOR)) {
		k_thread_foreach_unlocked(log_thread_info, NULL);
	}

	/* Save the current task id in panic info.
	 * The PANIC_SW_WATCHDOG_WARN reason will be changed to a regular
	 * PANIC_SW_WATCHDOG in system_common_pre_init if a watchdog reset
	 * occurs.
	 */
	panic_set_reason(PANIC_SW_WATCHDOG_WARN, exception_address, task_id);
}

__maybe_unused static void
wdt_warning_handler_with_enable(const struct device *wdt_dev, int channel_id)
{
	wdt_warning_handler(wdt_dev, channel_id);
	/* Watchdog is disabled after calling handler. Re-enable it now. */
	watchdog_enable(wdt_dev);
}
