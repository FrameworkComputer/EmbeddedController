/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/**
 * Terminates the thread, which could lead to system reboot.
 *
 * CONFIG_ARCH_POSIX enables Zephyr to use the host OS's standard C library.
 * Thus, it uses `exit()` for cleanup, unlike embedded targets that might use
 * `_exit()`. This ensures proper process termination in a POSIX environment.
 * The `_exit()` is a weak Stanadard C library function that can be overridden
 * similar to other functions such as abort().
 *
 * @param rc exit code
 */
#ifndef CONFIG_ARCH_POSIX
void _exit(int rc)
#else
void exit(int rc)
#endif
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
