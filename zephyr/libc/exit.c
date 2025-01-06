/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/**
 * Terminates the thread, which could lead to system reboot.
 *
 * This function is called from libc functions such as abort() or exit().
 *
 * @param rc exit code
 */
void _exit(int rc)
{
	k_tid_t thread = k_current_get();

#ifdef CONFIG_THREAD_NAME
	printk("%s exited with rc: %d\n", k_thread_name_get(thread), rc);
#else
	printk("%p exited with rc: %d\n", thread, rc);
#endif /* CONFIG_THREAD_NAME */

	k_thread_abort(thread);
	CODE_UNREACHABLE;
}
